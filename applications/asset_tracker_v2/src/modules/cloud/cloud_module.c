/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <stdio.h>
#include <zephyr/dfu/mcuboot.h>
#include <math.h>
#include <qos.h>

#if defined(CONFIG_NRF_CLOUD_AGPS)
#include <net/nrf_cloud_agps.h>
#endif
#if defined(CONFIG_NRF_CLOUD_PGPS)
#include <net/nrf_cloud_pgps.h>
#endif

#include "cloud_wrapper.h"
#include "data/cloud_codec/cloud_codec.h"

#define MODULE cloud_module

#include "module_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CLOUD_MODULE_LOG_LEVEL);

BUILD_ASSERT(CONFIG_CLOUD_CONNECT_RETRIES < 14,
	     "Cloud connect retries too large");

BUILD_ASSERT(IS_ENABLED(CONFIG_NRF_CLOUD_MQTT) ||
	     IS_ENABLED(CONFIG_AWS_IOT)	       ||
	     IS_ENABLED(CONFIG_AZURE_IOT_HUB)  ||
	     IS_ENABLED(CONFIG_LWM2M_INTEGRATION),
	     "A cloud transport service must be enabled");

#if defined(CONFIG_BOARD_QEMU_X86)
BUILD_ASSERT(IS_ENABLED(CONFIG_CLOUD_CLIENT_ID_USE_CUSTOM),
	     "Passing IMEI as cloud client ID is not supported when building for QEMU x86. "
	     "This is because IMEI is retrieved from the modem and not available when running "
	     "Zephyr's native TCP/IP stack.");
#endif

/* Cloud module super states. */
static enum state_type {
	STATE_LTE_INIT,
	STATE_LTE_DISCONNECTED,
	STATE_LTE_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Cloud module sub states. */
static enum sub_state_type {
	SUB_STATE_CLOUD_DISCONNECTED,
	SUB_STATE_CLOUD_CONNECTED
} sub_state;

static struct k_work_delayable connect_check_work;

struct cloud_backoff_delay_lookup {
	int delay;
};

/* Lookup table for backoff reconnection to cloud. Binary scaling. */
static struct cloud_backoff_delay_lookup backoff_delay[] = {
	{ 32 }, { 64 }, { 128 }, { 256 }, { 512 },
	{ 2048 }, { 4096 }, { 8192 }, { 16384 }, { 32768 },
	{ 65536 }, { 131072 }, { 262144 }, { 524288 }, { 1048576 }
};

/* Variable that keeps track of how many times a reconnection to cloud
 * has been tried without success.
 */
static int connect_retries;

/* Local copy of the device configuration. */
static struct cloud_data_cfg copy_cfg;
const k_tid_t cloud_module_thread;

/* Register message IDs that are used with the QoS library. */
QOS_MESSAGE_TYPES_REGISTER(GENERIC, BATCH, UI, NEIGHBOR_CELLS, AGPS_REQUEST,
			   PGPS_REQUEST, CONFIG, MEMFAULT);

/* Cloud module message queue. */
#define CLOUD_QUEUE_ENTRY_COUNT		20
#define CLOUD_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(cloud_module_msgq, sizeof(struct module_msg),
	      CLOUD_QUEUE_ENTRY_COUNT, CLOUD_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "cloud",
	.msg_q = &cloud_module_msgq,
	.supports_shutdown = true
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *cloud_module = &self;

/* Forward declarations. */
static void connect_check_work_fn(struct k_work *work);
static void send_config_received(void);
static void add_qos_message(uint8_t *ptr, size_t len, uint8_t type,
			    uint32_t flags, bool heap_allocated);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state) {
	case STATE_LTE_INIT:
		return "STATE_LTE_INIT";
	case STATE_LTE_DISCONNECTED:
		return "STATE_LTE_DISCONNECTED";
	case STATE_LTE_CONNECTED:
		return "STATE_LTE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_CLOUD_DISCONNECTED:
		return "SUB_STATE_CLOUD_DISCONNECTED";
	case SUB_STATE_CLOUD_CONNECTED:
		return "SUB_STATE_CLOUD_CONNECTED";
	default:
		return "Unknown";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state2str(sub_state),
		sub_state2str(new_state));

	sub_state = new_state;
}

/* Handlers */

static void config_data_handle(uint8_t *buf, const size_t len)
{
	int err;

	/* Use the config copy when populating the config variable
	 * before it is sent to the Data module. This way we avoid
	 * sending uninitialized variables to the Data module.
	 */
	err = cloud_codec_decode_config(buf, len, &copy_cfg);
	if (err == 0) {
		LOG_DBG("Device configuration encoded");
		send_config_received();
	} else if (err == -ENODATA) {
		LOG_WRN("Device configuration empty!");
		SEND_MSG(data_module, CLOUD_MSG_CONFIG_EMPTY);
	} else if (err == -ECANCELED) {
		/* The incoming message has already been handled, ignored. */
	} else if (err == -ENOENT) {
		/* Encoding of incoming message is not supported. */
	} else {
		LOG_ERR("Decoding of device configuration, error: %d", err);
		SEND_ERROR(CLOUD_MSG_ERROR, err);
	}
}

static void agps_data_handle(const uint8_t *buf, const size_t len)
{
#if defined(CONFIG_NRF_CLOUD_AGPS)
	int err = nrf_cloud_agps_process(buf, len);

	if (err) {
		LOG_ERR("Unable to process A-GPS data, error: %d", err);
		return;
	}
#if defined(CONFIG_NRF_CLOUD_PGPS)
	err = nrf_cloud_pgps_notify_prediction();
	if (err) {
		LOG_ERR("Error requesting prediction notification: %d", err);
		return;
	}
#endif /* CONFIG_NRF_CLOUD_PGPS */
#endif /* CONFIG_NRF_CLOUD_AGPS */
}

static void pgps_data_handle(const uint8_t *buf, const size_t len)
{
#if defined(CONFIG_NRF_CLOUD_PGPS)
#if !defined(CONFIG_NRF_CLOUD_PGPS_DOWNLOAD_TRANSPORT_CUSTOM)
	int err = nrf_cloud_pgps_process(buf, len);

	if (err) {
		LOG_ERR("Unable to process P-GPS data, error: %d", err);
		return;
	}

#endif /* CONFIG_NRF_CLOUD_PGPS_DOWNLOAD_TRANSPORT_CUSTOM */
#endif /* CONFIG_NRF_CLOUD_PGPS */
}

static void cloud_wrap_event_handler(const struct cloud_wrap_event *const evt)
{
	switch (evt->type) {
	case CLOUD_WRAP_EVT_CONNECTING: {
		LOG_DBG("CLOUD_WRAP_EVT_CONNECTING");
		SEND_MSG(ui_module, CLOUD_MSG_CONNECTING);
		break;
	}
	case CLOUD_WRAP_EVT_CONNECTED: {
		LOG_DBG("CLOUD_WRAP_EVT_CONNECTED");
		SEND_MSG_ALL(CLOUD_MSG_CONNECTED);
		break;
	}
	case CLOUD_WRAP_EVT_DISCONNECTED:
		LOG_DBG("CLOUD_WRAP_EVT_DISCONNECTED");
		SEND_MSG(&self, CLOUD_MSG_DISCONNECTED);
		SEND_MSG(data_module, CLOUD_MSG_DISCONNECTED);
		break;
	case CLOUD_WRAP_EVT_DATA_RECEIVED:
		LOG_DBG("CLOUD_WRAP_EVT_DATA_RECEIVED");
		config_data_handle(evt->data.buf, evt->data.len);
		break;
	case CLOUD_WRAP_EVT_PGPS_DATA_RECEIVED:
		LOG_DBG("CLOUD_WRAP_EVT_PGPS_DATA_RECEIVED");
		pgps_data_handle(evt->data.buf, evt->data.len);
		break;
	case CLOUD_WRAP_EVT_AGPS_DATA_RECEIVED:
		LOG_DBG("CLOUD_WRAP_EVT_AGPS_DATA_RECEIVED");
		agps_data_handle(evt->data.buf, evt->data.len);
		break;
	case CLOUD_WRAP_EVT_USER_ASSOCIATION_REQUEST: {
		LOG_DBG("CLOUD_WRAP_EVT_USER_ASSOCIATION_REQUEST");

		/* Cancel the reconnection routine upon a user association request. Device is
		 * awaiting registration to an nRF Cloud and does not need to reconnect
		 * until this happens.
		 */
		k_work_cancel_delayable(&connect_check_work);
		connect_retries = 0;

		SEND_MSG(modem_module, CLOUD_MSG_USER_ASSOCIATION_REQUEST);
		SEND_MSG(ui_module, CLOUD_MSG_USER_ASSOCIATION_REQUEST);

		break;
	}
	case CLOUD_WRAP_EVT_USER_ASSOCIATED: {
		LOG_DBG("CLOUD_WRAP_EVT_USER_ASSOCIATED");

		/* After user association, the device is disconnected. Reconnect immediately
		 * to complete the process.
		 */
		if (!k_work_delayable_is_pending(&connect_check_work)) {
			k_work_reschedule(&connect_check_work, K_SECONDS(5));
		}

		SEND_MSG(modem_module, CLOUD_MSG_USER_ASSOCIATED);
		SEND_MSG(ui_module, CLOUD_MSG_USER_ASSOCIATED);

		break;
	}
	case CLOUD_WRAP_EVT_REBOOT_REQUEST: {
		SEND_MSG(ui_module, CLOUD_MSG_REBOOT_REQUEST);

		break;
	}
	case CLOUD_WRAP_EVT_LTE_DISCONNECT_REQUEST: {
		SEND_MSG(modem_module, CLOUD_MSG_LTE_DISCONNECT);

		break;
	}
	case CLOUD_WRAP_EVT_LTE_CONNECT_REQUEST: {
		SEND_MSG(modem_module, CLOUD_MSG_LTE_CONNECT);

		break;
	}
	case CLOUD_WRAP_EVT_FOTA_DONE: {
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_DONE");

		SEND_MSG(ui_module, CLOUD_MSG_FOTA_DONE);
		SEND_MSG(util_module, CLOUD_MSG_FOTA_DONE);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_START: {
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_START");
		SEND_MSG(ui_module, CLOUD_MSG_FOTA_START);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_ERASE_PENDING:
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_ERASE_PENDING");
		break;
	case CLOUD_WRAP_EVT_FOTA_ERASE_DONE:
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_ERASE_DONE");
		break;
	case CLOUD_WRAP_EVT_FOTA_ERROR: {
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_ERROR");
		SEND_MSG(ui_module, CLOUD_MSG_FOTA_ERROR);
		break;
	}
	case CLOUD_WRAP_EVT_DATA_ACK: {
		LOG_DBG("CLOUD_WRAP_EVT_DATA_ACK: %d", evt->message_id);

		int err = qos_message_remove(evt->message_id);

		if (err == -ENODATA) {
			LOG_DBG("Message Acknowledgment not in pending QoS list, ID: %d",
				evt->message_id);
		} else if (err) {
			LOG_ERR("qos_message_remove, error: %d", err);
			SEND_ERROR(CLOUD_MSG_ERROR, err);
		}

		break;
	}
	case CLOUD_WRAP_EVT_PING_ACK: {
		LOG_DBG("CLOUD_WRAP_EVT_PING_ACK");

		/* Notify all messages upon a PING ACK. This means that there most likely is an
		 * established RCC connection and we can try to send all available messages.
		 */
		qos_message_notify_all();
		break;
	}
	case CLOUD_WRAP_EVT_ERROR: {
		LOG_DBG("CLOUD_WRAP_EVT_ERROR");
		SEND_ERROR(CLOUD_MSG_ERROR, evt->err);
		break;
	}
	default:
		break;

	}
}

/* Static module functions. */
static void send_config_received(void)
{
	int err;
	struct module_msg msg = {
		.type = CLOUD_MSG_CONFIG_RECEIVED,
		.cloud.config = copy_cfg,
	};

	err = module_send_msg(data_module, &msg);
	if (err) {
		LOG_ERR("Failed to send configuration update, error: %d", err);
	}
}

static void connect_cloud(void)
{
	int err;
	int backoff_sec = backoff_delay[connect_retries].delay;

	LOG_DBG("Connecting to cloud");

	if (connect_retries > CONFIG_CLOUD_CONNECT_RETRIES) {
		LOG_WRN("Too many failed cloud connection attempts");
		SEND_ERROR(CLOUD_MSG_ERROR, -ENETUNREACH);
		return;
	}

	/* The cloud will return error if cloud_wrap_connect() is called while
	 * the socket is polled on in the internal cloud thread or the
	 * cloud backend is the wrong state. We cannot treat this as an error as
	 * it is rather common that cloud_connect can be called under these
	 * conditions.
	 */
	err = cloud_wrap_connect();
	if (err) {
		LOG_ERR("cloud_connect failed, error: %d", err);
	}

	connect_retries++;

	LOG_WRN("Cloud connection establishment in progress");
	LOG_WRN("New connection attempt in %d seconds if not successful",
		backoff_sec);

	/* Start timer to check connection status after backoff */
	k_work_reschedule(&connect_check_work, K_SECONDS(backoff_sec));
}

static void disconnect_cloud(void)
{
	cloud_wrap_disconnect();

	connect_retries = 0;
	qos_timer_reset();

	k_work_cancel_delayable(&connect_check_work);
}

/* Convenience function used to add messages to the QoS library. */
static void add_qos_message(uint8_t *ptr, size_t len, uint8_t type,
			    uint32_t flags, bool heap_allocated)
{
	int err;
	struct qos_data message = {
		.heap_allocated = heap_allocated,
		.data.buf = ptr,
		.data.len = len,
		.id = qos_message_id_get_next(),
		.type = type,
		.flags = flags
	};

	err = qos_message_add(&message);
	if (err == -ENOMEM) {
		LOG_WRN("Cannot add message, internal pending list is full");
	} else if (err) {
		LOG_ERR("qos_message_add, error: %d", err);
		SEND_ERROR(CLOUD_MSG_ERROR, err);
	}
}

static void qos_event_handler(const struct qos_evt *evt)
{
	int err;
	struct module_msg msg = {
		.type = CLOUD_MSG_DATA_SEND_QOS,
		.cloud.config = copy_cfg,
	};
	bool enqueue_message = false;

	switch (evt->type) {
	case QOS_EVT_MESSAGE_NEW: {
		LOG_DBG("QOS_EVT_MESSAGE_NEW");

		msg.cloud.message = evt->message;
		enqueue_message = true;
	}
		break;
	case QOS_EVT_MESSAGE_TIMER_EXPIRED: {
		LOG_DBG("QOS_EVT_MESSAGE_TIMER_EXPIRED");

		msg.cloud.message = evt->message;
		enqueue_message = true;
	}
		break;
	case QOS_EVT_MESSAGE_REMOVED_FROM_LIST:
		LOG_DBG("QOS_EVT_MESSAGE_REMOVED_FROM_LIST");

		if (evt->message.heap_allocated) {
			LOG_DBG("Freeing pointer: %p", (void *)evt->message.data.buf);
			k_free(evt->message.data.buf);
		}

		break;
	default:
		LOG_DBG("Unknown QoS handler event");

		break;
	}

	if (enqueue_message) {
		err = module_enqueue_msg(&self, &msg);
		if (err) {
			LOG_ERR("Failed to send configuration update, error: %d", err);
		}
	}
}

/* If this work is executed, it means that the connection attempt was not
 * successful before the backoff timer expired. A timeout message is then
 * added to the message queue to signal the timeout.
 */
static void connect_check_work_fn(struct k_work *work)
{
	if ((state == STATE_LTE_CONNECTED && sub_state == SUB_STATE_CLOUD_CONNECTED) ||
	    (state == STATE_LTE_DISCONNECTED)) {
		return;
	}

	LOG_DBG("Cloud connection timeout occurred");

	(void)SEND_MSG(&self, CLOUD_MSG_CONNECTION_TIMEOUT);
}

static int setup(void)
{
	int err;

	err = cloud_wrap_init(cloud_wrap_event_handler);
	if (err) {
		LOG_ERR("cloud_wrap_init, error: %d", err);
		return err;
	}

	err = qos_init(qos_event_handler);
	if (err) {
		LOG_ERR("qos_init, error: %d", err);
		return err;
	}

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	/* After a successful initializaton, tell the bootloader that the
	 * current image is confirmed to be working.
	 */
	boot_write_img_confirmed();
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */

	return 0;
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct module_msg *msg)
{
	if ((IS_MSG(msg, MODEM_MSG_INITIALIZED)) ||
	    (IS_MSG(msg, DEBUG_MSG_QEMU_X86_INITIALIZED))) {
		int err;

		state_set(STATE_LTE_DISCONNECTED);

		err = setup();
		__ASSERT(err == 0, "setp() failed");
	}
}

/* Message handler for STATE_LTE_CONNECTED. */
static void on_state_lte_connected(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_LTE_DISCONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		state_set(STATE_LTE_DISCONNECTED);

		/* Explicitly disconnect cloud when you receive an LTE disconnected event.
		 * This is to clear up the cloud library state.
		 */
		disconnect_cloud();
	}

	if (IS_MSG(msg, MODEM_MSG_CARRIER_FOTA_PENDING)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		disconnect_cloud();
	}

	if (IS_MSG(msg, MODEM_MSG_CARRIER_FOTA_STOPPED)) {
		connect_cloud();
	}
}

/* Message handler for STATE_LTE_DISCONNECTED. */
static void on_state_lte_disconnected(struct module_msg *msg)
{
	if ((IS_MSG(msg, MODEM_MSG_LTE_CONNECTED)) ||
	    (IS_MSG(msg, DEBUG_MSG_QEMU_X86_NETWORK_CONNECTED))) {
		state_set(STATE_LTE_CONNECTED);

		/* LTE is now connected, cloud connection can be attempted */
		connect_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_CONNECTED. */
static void on_sub_state_cloud_connected(struct module_msg *msg)
{
	int err = 0;

	if (IS_MSG(msg, CLOUD_MSG_DISCONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);

		k_work_reschedule(&connect_check_work, K_SECONDS(1));

		/* Reset QoS timer. Will be restarted upon a successful call to qos_message_add() */
		qos_timer_reset();
		return;
	}

	if (IS_MSG(msg, DATA_MSG_CONFIG_GET)) {
		/* The device will get its configuration if it has changed, for every update.
		 * Due to this we don't use the QoS library.
		 */
		err = cloud_wrap_state_get(false, 0);
		if (err == -ENOTSUP) {
			LOG_DBG("Requesting of device configuration is not supported");
		} else if (err) {
			LOG_ERR("cloud_wrap_state_get, err: %d", err);
		} else {
			LOG_DBG("Device configuration requested");
		}
	}

	if (IS_MSG(msg, DEBUG_MSG_MEMFAULT_DATA_READY)) {
		add_qos_message(msg->debug.memfault.buf,
				msg->debug.memfault.len,
				MEMFAULT,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, DATA_MSG_AGPS_REQUEST_DATA_SEND)) {

		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
			int err = cloud_wrap_agps_request_send(NULL, 0, true, 0);
			if (err) {
				LOG_ERR("cloud_wrap_agps_request_send, err: %d", err);
			}

			return;
		}

		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				AGPS_REQUEST,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, DATA_MSG_DATA_SEND)) {

		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {

			char *paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];

			__ASSERT(ARRAY_SIZE(paths) ==
				 ARRAY_SIZE(msg->data.buffer.paths),
				 "Path object list not the same size");

			for (int i = 0; i < ARRAY_SIZE(paths); i++) {
				paths[i] = msg->data.buffer.paths[i];
			}

			err = cloud_wrap_data_send(NULL,
						   msg->data.buffer.valid_object_paths,
						   true,
						   0,
						   paths);
			if (err) {
				LOG_ERR("cloud_wrap_data_send, err: %d", err);
			}

			return;
		}

		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				GENERIC,
				QOS_FLAG_RELIABILITY_ACK_DISABLED,
				true);
	}

	if (IS_MSG(msg, DATA_MSG_CONFIG_SEND)) {
		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				CONFIG,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, DATA_MSG_DATA_SEND_BATCH)) {
		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				BATCH,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if ((IS_MSG(msg, DATA_MSG_UI_DATA_SEND)) ||
	    (IS_MSG(msg, DATA_MSG_IMPACT_DATA_SEND))) {

		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {

			char *paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];

			__ASSERT(ARRAY_SIZE(paths) ==
				 ARRAY_SIZE(msg->data.buffer.paths),
				 "Path object list not the same size");

			for (int i = 0; i < ARRAY_SIZE(paths); i++) {
				paths[i] = msg->data.buffer.paths[i];
			}

			err = cloud_wrap_ui_send(NULL,
						 msg->data.buffer.valid_object_paths,
						 true,
						 0,
						 paths);
			if (err) {
				LOG_ERR("cloud_wrap_ui_send, err: %d", err);
			}

			return;
		}

		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				UI,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, DATA_MSG_NEIGHBOR_CELLS_DATA_SEND)) {

		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
			err = cloud_wrap_neighbor_cells_send(NULL, 0, true, 0);
			if (err) {
				LOG_ERR("cloud_wrap_neighbor_cells_send, err: %d", err);
			}

			return;
		}

		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				NEIGHBOR_CELLS,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, CLOUD_MSG_DATA_SEND_QOS)) {

		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
			return;
		}

		bool ack = qos_message_has_flag(&msg->cloud.message,
						QOS_FLAG_RELIABILITY_ACK_REQUIRED);

		qos_message_print(&msg->cloud.message);

		struct qos_payload *message = &msg->cloud.message.data;

		switch (msg->cloud.message.type) {
		case GENERIC:
			err = cloud_wrap_data_send(message->buf,
						   message->len,
						   ack,
						   msg->cloud.message.id,
						   NULL);
			if (err) {
				LOG_WRN("cloud_wrap_data_send, err: %d", err);
			}
			break;
		case BATCH:
			err = cloud_wrap_batch_send(message->buf,
						    message->len,
						    ack,
						    msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_batch_send, err: %d", err);
			}
			break;
		case UI:
			err = cloud_wrap_ui_send(message->buf,
						 message->len,
						 ack,
						 msg->cloud.message.id,
						 NULL);
			if (err) {
				LOG_WRN("cloud_wrap_ui_send, err: %d", err);
			}
			break;
		case NEIGHBOR_CELLS:
			err = cloud_wrap_neighbor_cells_send(message->buf,
							     message->len,
							     ack,
							     msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_neighbor_cells_send, err: %d", err);
			}
			break;
		case AGPS_REQUEST:
			err = cloud_wrap_agps_request_send(message->buf,
							   message->len,
							   ack,
							   msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_agps_request_send, err: %d", err);
			}
			break;
		case PGPS_REQUEST:
			err = cloud_wrap_pgps_request_send(message->buf,
							   message->len,
							   ack,
							   msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_pgps_request_send, err: %d", err);
			}
			break;
		case CONFIG:
			err = cloud_wrap_state_send(message->buf,
						    message->len,
						    ack,
						    msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_state_send, err: %d", err);
			}
			break;
		case MEMFAULT:
			err = cloud_wrap_memfault_data_send(message->buf,
							    message->len,
							    ack,
							    msg->cloud.message.id);
			if (err) {
				LOG_WRN("cloud_wrap_memfault_data_send, err: %d", err);
			}
			break;
		default:
			LOG_WRN("Unknown data type");
			break;
		}
	}
}

/* Message handler for SUB_STATE_CLOUD_DISCONNECTED. */
static void on_sub_state_cloud_disconnected(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_CONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_CONNECTED);

		connect_retries = 0;
		k_work_cancel_delayable(&connect_check_work);
	}

	if (IS_MSG(msg, CLOUD_MSG_CONNECTION_TIMEOUT)) {
		connect_cloud();
	}

	/* The initial device configuration acknowledgment typically occurs before
	 * the nRF Cloud library has notified that it has established a full connection to
	 * the cloud service. Due to this we allow data to be sent
	 * in the SUB_STATE_CLOUD_DISCONNECTED state.
	 */
	if (IS_MSG(msg, DATA_MSG_CONFIG_SEND) &&
	    IS_ENABLED(CONFIG_NRF_CLOUD_MQTT)) {
		add_qos_message(msg->data.buffer.buf,
				msg->data.buffer.len,
				CONFIG,
				QOS_FLAG_RELIABILITY_ACK_REQUIRED,
				true);
	}

	if (IS_MSG(msg, CLOUD_MSG_DATA_SEND_QOS) &&
	    IS_ENABLED(CONFIG_NRF_CLOUD_MQTT)) {
		bool ack = qos_message_has_flag(&msg->cloud.message,
						QOS_FLAG_RELIABILITY_ACK_REQUIRED);

		qos_message_print(&msg->cloud.message);

		struct qos_payload *message = &msg->cloud.message.data;

		switch (msg->cloud.message.type) {
		case CONFIG: {
			int err = cloud_wrap_state_send(message->buf, message->len, ack,
							msg->cloud.message.id);

			if (err) {
				LOG_WRN("cloud_wrap_state_send, err: %d", err);
			}
		}
			break;
		default:
			LOG_WRN("Unknown data type");
			break;
		}
	}
}

/* Message handler for all states. */
static void on_all_states(struct module_msg *msg)
{
	switch (msg->type) {
	case UTIL_MSG_SHUTDOWN_REQUEST:
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(CLOUD_MSG_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
		break;
	case DATA_MSG_CONFIG_INIT:
		/* Fall through. */
	case DATA_MSG_CONFIG_READY:
		copy_cfg = msg->data.cfg;
		break;
	default:
		break;
	}

#if defined(CONFIG_NRF_CLOUD_PGPS)
	if (IS_EVENT(msg, location, LOCATION_MODULE_EVT_PGPS_NEEDED)) {
		/* Encode and send P-GPS request to cloud. */
		struct cloud_codec_data output = {0};
		struct cloud_data_pgps_request request = {
			.count = msg->module.location.data.pgps_request.prediction_count,
			.interval = msg->module.location.data.pgps_request.prediction_period_min,
			.day = msg->module.location.data.pgps_request.gps_day,
			.time = msg->module.location.data.pgps_request.gps_time_of_day,
			.queued = true,
		};

		int err = cloud_codec_encode_pgps_request(&output, &request);

		switch (err) {
		case 0:
			LOG_DBG("P-GPS request encoded successfully");

			if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
				int err = cloud_wrap_pgps_request_send(NULL,
								       0,
								       true,
								       0);
				if (err) {
					LOG_ERR("cloud_wrap_pgps_request_send, err: %d", err);
				}

				return;
			}

			add_qos_message(output.buf,
					output.len,
					PGPS_REQUEST,
					QOS_FLAG_RELIABILITY_ACK_REQUIRED,
					true);
			break;
		case -ENOTSUP:
			LOG_DBG("P-GPS request encoding is not supported, error: %d", err);
			break;
		case -ENODATA:
			LOG_DBG("No P-GPS data to encode, error: %d", err);
			break;
		default:
			LOG_ERR("Error encoding P-GPS request: %d", err);
			SEND_ERROR(data, DATA_EVT_ERROR, err);
			return;
		}
	}
#endif
}

static void module_thread_fn(void)
{
	int err;
	struct module_msg msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(CLOUD_MSG_ERROR, err);
	}

	state_set(STATE_LTE_INIT);
	sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);

	k_work_init_delayable(&connect_check_work, connect_check_work_fn);

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_LTE_INIT:
			on_state_init(&msg);
			break;
		case STATE_LTE_CONNECTED:
			switch (sub_state) {
			case SUB_STATE_CLOUD_CONNECTED:
				on_sub_state_cloud_connected(&msg);
				break;
			case SUB_STATE_CLOUD_DISCONNECTED:
				on_sub_state_cloud_disconnected(&msg);
				break;
			default:
				LOG_ERR("Unknown Cloud module sub state");
				break;
			}

			on_state_lte_connected(&msg);
			break;
		case STATE_LTE_DISCONNECTED:
			on_state_lte_disconnected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_ERR("Unknown Cloud module state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(cloud_module_thread, CONFIG_CLOUD_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
