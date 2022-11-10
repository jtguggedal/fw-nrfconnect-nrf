/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(CONFIG_NRF_MODEM_LIB)
#include <modem/nrf_modem_lib.h>
#endif /* CONFIG_NRF_MODEM_LIB */
#include <zephyr/sys/reboot.h>
#if defined(CONFIG_LWM2M_INTEGRATION)
#include <net/lwm2m_client_utils.h>
#endif /* CONFIG_LWM2M_INTEGRATION */
#include <net/nrf_cloud.h>

#if defined(CONFIG_NRF_CLOUD_AGPS) || defined(CONFIG_NRF_CLOUD_PGPS)
#include <net/nrf_cloud_agps.h>
#endif

#include "module_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_APPLICATION_MODULE_LOG_LEVEL);

/* Application module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

/* Application sub states. The application can be in either active or passive
 * mode.
 *
 * Active mode: Sensor data and GNSS position is acquired at a configured
 *		interval and sent to cloud.
 *
 * Passive mode: Sensor data and GNSS position is acquired when movement is
 *		 detected, or after the configured movement timeout occurs.
 */
static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

/* Internal copy of the device configuration. */
static struct cloud_data_cfg app_cfg;

/* Variable that keeps track whether modem static data has been successfully sampled by the
 * modem module. Modem static data does not change and only needs to be sampled and sent to cloud
 * once.
 */
static bool modem_static_sampled;

/* Timer callback used to signal when timeout has occurred both in active
 * and passive mode.
 */
static void data_sample_timer_handler(struct k_timer *timer);

/* Timer callback used to reset the activity trigger flag */
static void movement_resolution_timer_handler(struct k_timer *timer);

/* Application module message queue. */
#define APP_QUEUE_ENTRY_COUNT		10
#define APP_QUEUE_BYTE_ALIGNMENT	4

/* Data fetching timeouts */
#define DATA_FETCH_TIMEOUT_DEFAULT 2

/* Flag to prevent multiple activity events within one movement resolution cycle */
static bool activity_triggered = true;
static bool inactivity_triggered = true;

K_MSGQ_DEFINE(msgq_app, sizeof(struct module_msg), APP_QUEUE_ENTRY_COUNT,
	      APP_QUEUE_BYTE_ALIGNMENT);

/* Data sample timer used in active mode. */
K_TIMER_DEFINE(data_sample_timer, data_sample_timer_handler, NULL);

/* Movement timer used to detect movement timeouts in passive mode. */
K_TIMER_DEFINE(movement_timeout_timer, data_sample_timer_handler, NULL);

/* Movement resolution timer decides the period after movement that consecutive
 * movements are ignored and do not cause data collection. This is used to
 * lower power consumption by limiting how often GNSS search is performed and
 * data is sent on air.
 */
K_TIMER_DEFINE(movement_resolution_timer, movement_resolution_timer_handler, NULL);

/* Module data structure to hold information of the application module, which
 * opens up for using convenience functions available for modules.
 */
static struct module_data self = {
	.name = "app",
	.msg_q = &msgq_app,
	.supports_shutdown = true,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *app_module = &self;

#if defined(CONFIG_NRF_MODEM_LIB)
NRF_MODEM_LIB_ON_INIT(asset_tracker_init_hook, on_modem_lib_init, NULL);

/* Initialized to value different than success (0) */
static int modem_lib_init_result = -1;

static void on_modem_lib_init(int ret, void *ctx)
{
	modem_lib_init_result = ret;
}
#endif /* CONFIG_NRF_MODEM_LIB */

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
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

/* Check the return code from nRF modem library initialization to ensure that
 * the modem is rebooted if a modem firmware update is ready to be applied or
 * an error condition occurred during firmware update or library initialization.
 */
static void handle_nrf_modem_lib_init_ret(void)
{
#if defined(CONFIG_NRF_MODEM_LIB)
	int ret = modem_lib_init_result;

	/* Handle return values relating to modem firmware update */
	switch (ret) {
	case 0:
		/* Initialization successful, no action required. */
		return;
	case MODEM_DFU_RESULT_OK:
		LOG_WRN("MODEM UPDATE OK. Will run new modem firmware after reboot");
		break;
	case MODEM_DFU_RESULT_UUID_ERROR:
	case MODEM_DFU_RESULT_AUTH_ERROR:
		LOG_ERR("MODEM UPDATE ERROR %d. Will run old firmware", ret);
		break;
	case MODEM_DFU_RESULT_HARDWARE_ERROR:
	case MODEM_DFU_RESULT_INTERNAL_ERROR:
		LOG_ERR("MODEM UPDATE FATAL ERROR %d. Modem failure", ret);
		break;
	default:
		/* All non-zero return codes other than DFU result codes are
		 * considered irrecoverable and a reboot is needed.
		 */
		LOG_ERR("nRF modem lib initialization failed, error: %d", ret);
		break;
	}

#if defined(CONFIG_NRF_CLOUD_FOTA)
	/* Ignore return value, rebooting below */
	(void)nrf_cloud_fota_pending_job_validate(NULL);
#elif defined(CONFIG_LWM2M_INTEGRATION)
	lwm2m_verify_modem_fw_update();
#endif
	LOG_WRN("Rebooting...");
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
#endif /* CONFIG_NRF_MODEM_LIB */
}

static void data_sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	SEND_MSG_ALL(APP_MSG_DATA_GET_ALL);
}

static void movement_resolution_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	activity_triggered = false;
	inactivity_triggered = false;
}

/* Static module functions. */
static void passive_mode_timers_start_all(void)
{
	LOG_DBG("Device mode: Passive");
	LOG_DBG("Start movement timeout: %d seconds interval", app_cfg.movement_timeout);

	LOG_DBG("%d seconds until movement can trigger a new data sample/publication",
		app_cfg.movement_resolution);

	k_timer_start(&movement_resolution_timer,
		      K_SECONDS(app_cfg.movement_resolution),
		      K_SECONDS(0));

	k_timer_start(&movement_timeout_timer,
		      K_SECONDS(app_cfg.movement_timeout),
		      K_SECONDS(app_cfg.movement_timeout));

	k_timer_stop(&data_sample_timer);
}

static void active_mode_timers_start_all(void)
{
	LOG_DBG("Device mode: Active");
	LOG_DBG("Start data sample timer: %d seconds interval", app_cfg.active_wait_timeout);

	k_timer_start(&data_sample_timer,
		      K_SECONDS(app_cfg.active_wait_timeout),
		      K_SECONDS(app_cfg.active_wait_timeout));

	k_timer_stop(&movement_resolution_timer);
	k_timer_stop(&movement_timeout_timer);
}

static void data_get(void)
{
	int err;
	size_t count = 0;
	struct module_msg msg = {
		.type = APP_MSG_DATA_GET,
	};

	/* Set a low sample timeout. If location is requested, the sample timeout
	 * will be increased to accommodate the location request.
	 */
	msg.app.timeout = DATA_FETCH_TIMEOUT_DEFAULT;

	/* Specify which data that is to be included in the transmission. */
	msg.app.data_list[count++] = APP_DATA_MODEM_DYNAMIC;
	msg.app.data_list[count++] = APP_DATA_BATTERY;
	msg.app.data_list[count++] = APP_DATA_ENVIRONMENTAL;

	if (!modem_static_sampled) {
		msg.app.data_list[count++] = APP_DATA_MODEM_STATIC;
	}

	if (!app_cfg.no_data.neighbor_cell || !app_cfg.no_data.gnss) {
		msg.app.data_list[count++] = APP_DATA_LOCATION;

		/* Set application module timeout when location sampling is requested.
		 * This is selected to be long enough so that most of the GNSS would
		 * have enough time to run to get a fix. We also want it to be smaller than
		 * the sampling interval (120s). So, 110s was selected but we take
		 * minimum of sampling interval minus 5 (just some selected number) and 110.
		 * And mode (active or passive) is taken into account.
		 * If the timeout would become smaller than 5s, we want to ensure some time for
		 * the modules so the minimum value for application module timeout is 5s.
		 */
		msg.app.timeout = (app_cfg.active_mode) ?
			MIN(app_cfg.active_wait_timeout - 5, 110) :
			MIN(app_cfg.movement_resolution - 5, 110);
		msg.app.timeout = MAX(msg.app.timeout, 5);
	}

	/* Set list count to number of data types passed in app_module_event. */
	msg.app.count = count;

	err = module_send_msg_all(&msg);
	if (err) {
		LOG_ERR("Failed to send module_msg_GET, error: %d", err);
	}
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_CONFIG_INIT)) {
		/* Keep a copy of the new configuration. */
		app_cfg = msg->data.cfg;

		if (app_cfg.active_mode) {
			active_mode_timers_start_all();
		} else {
			passive_mode_timers_start_all();
		}

		state_set(STATE_RUNNING);
		sub_state_set(app_cfg.active_mode ? SUB_STATE_ACTIVE_MODE :
						    SUB_STATE_PASSIVE_MODE);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_CONNECTED)) {
		data_get();
	}

	if (IS_MSG(msg, APP_MSG_DATA_GET_ALL)) {
		data_get();
	}
}

/* Message handler for SUB_STATE_PASSIVE_MODE. */
void on_sub_state_passive(struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_CONFIG_READY)) {
		/* Keep a copy of the new configuration. */
		app_cfg = msg->data.cfg;

		if (app_cfg.active_mode) {
			active_mode_timers_start_all();
			sub_state_set(SUB_STATE_ACTIVE_MODE);
			return;
		}

		passive_mode_timers_start_all();
	}

	if ((IS_MSG(msg, UI_MSG_BUTTON_DATA_READY)) ||
	    (IS_MSG(msg, SENSOR_MSG_MOVEMENT_ACTIVITY_DETECTED)) ||
	    (IS_MSG(msg, SENSOR_MSG_MOVEMENT_IMPACT_DETECTED))) {
		if (IS_MSG(msg, UI_MSG_BUTTON_DATA_READY) &&
		    msg->ui.btn.button_number != 2) {
			return;
		}

		if (IS_MSG(msg, SENSOR_MSG_MOVEMENT_ACTIVITY_DETECTED)) {
			if (activity_triggered) {
				return;
			}
			activity_triggered = true;
		}

		/* Trigger a sample request if button 2 has been pushed on the DK or activity has
		 * been detected. The data request can only be triggered if the movement
		 * resolution timer has timed out.
		 */

		if (k_timer_remaining_get(&movement_resolution_timer) == 0) {
			data_sample_timer_handler(NULL);
			passive_mode_timers_start_all();
		}
	}
	if (IS_MSG(msg, SENSOR_MSG_MOVEMENT_INACTIVITY_DETECTED)
	    && k_timer_remaining_get(&movement_resolution_timer) != 0) {
		/* Trigger a sample request if there has been inactivity after
		 * activity was triggered.
		 */
		if (!inactivity_triggered) {
			data_sample_timer_handler(NULL);
			inactivity_triggered = true;
		}
	}
}

/* Message handler for SUB_STATE_ACTIVE_MODE. */
static void on_sub_state_active(struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_CONFIG_READY)) {
		/* Keep a copy of the new configuration. */
		app_cfg = msg->data.cfg;

		if (!app_cfg.active_mode) {
			passive_mode_timers_start_all();
			sub_state_set(SUB_STATE_PASSIVE_MODE);
			return;
		}

		active_mode_timers_start_all();
	}
}

/* Message handler for all states. */
static void on_all_events(struct module_msg *msg)
{
	if (IS_MSG(msg, UTIL_MSG_SHUTDOWN_REQUEST)) {
		k_timer_stop(&data_sample_timer);
		k_timer_stop(&movement_timeout_timer);
		k_timer_stop(&movement_resolution_timer);

		SEND_SHUTDOWN_ACK(APP_MSG_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if (IS_MSG(msg, MODEM_MSG_MODEM_STATIC_DATA_READY)) {
		modem_static_sampled = true;
	}
}

void main(void)
{
	int err;
	struct module_msg msg;

	if (!IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
		handle_nrf_modem_lib_init_ret();
	}

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(APP_MSG_ERROR, err);
	}

	k_sleep(K_SECONDS(5));

	SEND_MSG_ALL(APP_MSG_START);

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			switch (sub_state) {
			case SUB_STATE_ACTIVE_MODE:
				on_sub_state_active(&msg);
				break;
			case SUB_STATE_PASSIVE_MODE:
				on_sub_state_passive(&msg);
				break;
			default:
				LOG_WRN("Unknown application sub state");
				break;
			}

			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown application state");
			break;
		}

		on_all_events(&msg);
	}
}
