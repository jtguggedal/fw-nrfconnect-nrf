/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <net/azure_iot_hub.h>
#include <power/reboot.h>
#include <dfu/mcuboot.h>
#include <cJSON.h>
#include <cJSON_os.h>

#if IS_ENABLED(CONFIG_LTE_LINK_CONTROL)
#include <modem/lte_lc.h>
#endif

/* Interval [s] between sending events to the IoT Hub. The value can be changed
 * by setting a new desired value for property 'telemetryInterval' in the
 * device twin document.
 */
#define EVENT_INTERVAL		20
#define RECV_BUF_SIZE		300

struct direct_method_data {
	struct k_delayed_work work;
	uint32_t request_id;
} direct_method_data;
struct k_delayed_work twin_report_work;
struct k_delayed_work send_event_work;

static char recv_buf[RECV_BUF_SIZE];
static void direct_method_handler(struct k_work *work);
static K_SEM_DEFINE(network_connected_sem, 0, 1);
static K_SEM_DEFINE(recv_buf_sem, 1, 1);
static atomic_t event_interval = EVENT_INTERVAL;
static bool network_connected;


/* Returns a positive integer if the new interval can be parsed, otherwise -1 */
static int event_interval_get(char *buf)
{
	struct cJSON *root_obj, *desired_obj, *event_interval_obj;
	int new_interval = -1;

	root_obj = cJSON_Parse(buf);
	if (root_obj == NULL) {
		printk("Could not parse properties object\n");
		return -1;
	}

	/* If the incoming buffer is a notification from the cloud about changes
	 * made to the device twin's "desired" properties, the root object will
	 * only contain the newly changed properties, and can be trated as if
	 * is the "desired" object.
	 * If the incoming is the response to a request to receive the device
	 * twin, it will contain a "desired" object and a "reported" object,
	 * and we need to access that object instead of the root.
	 */
	desired_obj = cJSON_GetObjectItem(root_obj, "desired");
	if (desired_obj == NULL) {
		desired_obj = root_obj;
	}

	/* Update only recognized properties. */
	event_interval_obj = cJSON_GetObjectItem(desired_obj, "telemetryInterval");
	if (event_interval_obj == NULL) {
		printk("No telemetryInterval object found");
		goto clean_exit;
	}

	if (cJSON_IsString(event_interval_obj)) {
		new_interval = atoi(event_interval_obj->valuestring);
	} else if (cJSON_IsNumber(event_interval_obj)) {
		new_interval = event_interval_obj->valueint;
	} else {
		printk("Invalid telemetry interval format received\n");
		goto clean_exit;
	}

clean_exit:
	cJSON_Delete(root_obj);
	k_sem_give(&recv_buf_sem);

	return new_interval;
}

static void event_interval_apply(int interval)
{
	atomic_set(&event_interval, interval);

	if (interval <= 0) {
		k_delayed_work_cancel(&send_event_work);
		printk("New event interval is %d, event reporting will stop\n",
		       interval);
		return;
	}

	k_delayed_work_submit(&send_event_work, K_NO_WAIT);
}

static void azure_event_handler(struct azure_iot_hub_evt *const evt)
{
	switch (evt->type) {
	case AZURE_IOT_HUB_EVT_CONNECTING:
		printk("AZURE_IOT_HUB_EVT_CONNECTING\n");
		break;
	case AZURE_IOT_HUB_EVT_CONNECTED:
		printk("AZURE_IOT_HUB_EVT_CONNECTED\n");
		break;
	case AZURE_IOT_HUB_EVT_DISCONNECTED:
		printk("AZURE_IOT_HUB_EVT_DISCONNECTED\n");
		break;
	case AZURE_IOT_HUB_EVT_READY:
		printk("AZURE_IOT_HUB_EVT_READY\n");
		k_delayed_work_submit(&send_event_work, K_SECONDS(3));
		break;
	case AZURE_IOT_HUB_EVT_DATA_RECEIVED:
		printk("AZURE_IOT_HUB_EVT_DATA_RECEIVED\n");
		break;
	case AZURE_IOT_HUB_EVT_DPS_STARTED:
		printk("AZURE_IOT_HUB_EVT_DPS_STARTED\n");
		break;
	case AZURE_IOT_HUB_EVT_DPS_DONE:
		printk("AZURE_IOT_HUB_EVT_DPS_DONE\n");
		break;
	case AZURE_IOT_HUB_EVT_DPS_FAILED:
		printk("AZURE_IOT_HUB_EVT_DPS_FAILED\n");
		break;
	case AZURE_IOT_HUB_EVT_TWIN:
		printk("AZURE_IOT_HUB_EVT_TWIN\n");
		event_interval_apply(event_interval_get(evt->data.msg.ptr));
		break;
	case AZURE_IOT_HUB_EVT_TWIN_DESIRED:
		printk("AZURE_IOT_HUB_EVT_TWIN_DESIRED\n");
		printf("Desired device property: %.*s\n",
			evt->data.msg.len,
			evt->data.msg.ptr);
		if (k_sem_take(&recv_buf_sem, K_NO_WAIT) == 0) {
			if (evt->data.msg.len > sizeof(recv_buf) - 1) {
				printk("Incoming data too big for buffer");
				break;
			}

			memcpy(recv_buf, evt->data.msg.ptr, evt->data.msg.len);
			recv_buf[evt->data.msg.len] = '\0';
			k_delayed_work_submit(&twin_report_work, K_SECONDS(1));
		} else {
			printk("Recv buffer is busy, data was not copied\n");
		}
		break;
	case AZURE_IOT_HUB_EVT_DIRECT_METHOD:
		printk("AZURE_IOT_HUB_EVT_DIRECT_METHOD\n");
		printk("Method name: %s\n", evt->data.method.name);
		printf("Payload: %.*s\n", evt->data.method.payload_len,
			evt->data.method.payload);

		direct_method_data.request_id = evt->data.method.rid;

		k_delayed_work_submit(&direct_method_data.work, K_SECONDS(1));

		break;
	case AZURE_IOT_HUB_EVT_TWIN_RESULT_SUCCESS:
		printk("AZURE_IOT_HUB_EVT_TWIN_RESULT_SUCCESS, ID: %d\n",
		       evt->data.result.rid);
		break;
	case AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL:
		printk("AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL, ID %d, status %d\n",
			evt->data.result.rid, evt->data.result.status);
		break;
	default:
		printk("Unknown Azure IoT Hub event type: %d\n", evt->type);
		break;
	}
}

/* For simplicity the functions below that send data to cloud are in this
 * sample executed from the system workqueue. Depending on what's supposed to be
 * done by the methods, this may or may not be suitable for other projects.
 * The system workqueue is used by many subsystems, drivers and libraries, and
 * should not perform tasks that take a long time. Some alternatives to system
 * workqueue are user-defined workqueues, a separate thread or using
 * the main thread.
 *
 * Note that functions that send data are usually blocking by default, which
 * may cause functions like azure_iot_hub_method_respond() and
 * azure_iot_hub_send() to hang for a considerable amount of time in
 * less-than-ideal network conditions.
 */

static void send_event(struct k_work *work)
{
	int err;
	static char buf[60];
	ssize_t len;
	struct azure_iot_hub_data msg = {
		.topic.type = AZURE_IOT_HUB_TOPIC_EVENT,
		.ptr = buf,
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
	};

	len = snprintf(buf, sizeof(buf),
		       "{\"temperature\":%d.%d,\"timestamp\":%d}",
		       25, k_uptime_get_32() % 10, k_uptime_get_32());
	if ((len < 0) || (len > sizeof(buf))) {
		printk("Failed to populate event buffer\n");
		goto exit;
	}

	msg.len = len;

	err = azure_iot_hub_send(&msg);
	if (err) {
		printk("Failed to send direct method response\n");
		goto exit;
	}

	printk("Event was successfully sent\n");
exit:
	if (atomic_get(&event_interval) <= 0) {
		printk("The event reporting stops, interval is set to %d\n",
		        atomic_get(&event_interval));
		return;
	}

	printk("Next event will be sent in %d seconds\n", event_interval);
	k_delayed_work_submit(&send_event_work, K_SECONDS(event_interval));
}

static void direct_method_handler(struct k_work *work)
{
	int err;
	static char ret[] = "{\"this\":\"worked\"}";
	struct azure_iot_hub_result result = {
		.rid = direct_method_data.request_id,
		.status = 200,
		.payload = ret,
		.payload_len = sizeof(ret),
	};

	err = azure_iot_hub_method_respond(&result);
	if (err) {
		printk("Failed to send direct method response");
	}
}

static void twin_report_work_fn(struct k_work *work)
{
	int err;
	char buf[100];
	ssize_t len;
	struct azure_iot_hub_data data = {
		.topic.type = AZURE_IOT_HUB_TOPIC_TWIN_REPORT,
		.ptr = buf,
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
	};
	int new_interval;

	new_interval = event_interval_get(recv_buf);
	if (new_interval < 0) {
		return;
	}

	len = snprintk(buf, sizeof(buf),
		       "{\"telemetryInterval\":%d}", new_interval);
	if (len <= 0) {
		printk("Failed to create twin report\n");
		return;
	}

	data.len = len;

	err = azure_iot_hub_send(&data);
	if (err) {
		printk("Failed to send twin report");
		return;
	}

	/* Note that the new interval value is first applied here, as that will
	 * make the "reported" value in the device twin be in sync with
	 * the reality on the device. Other applications may decide
	 * to apply the desired properties regardless of if the value is
	 * successfully reported or not.
	 */
	event_interval_apply(new_interval);
	printk("New telemetry interval has been applied: %d\n",  new_interval);
}

#if IS_ENABLED(CONFIG_LTE_LINK_CONTROL)

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			if (!network_connected) {
				break;
			}

			network_connected = false;

			printk("LTE network is disconnected.\n");
			printk("Subsequent sending of data may block or fail.");
			break;
		}

		printk("Network registration status: %s\n",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming");
		k_sem_give(&network_connected_sem);
		network_connected = true;
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		printk("PSM parameter update: TAU: %d, Active time: %d\n",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			printk("%s\n", log_buf);
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		printk("RRC mode: %s\n",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
			evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static void modem_configure(void)
{
	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		int err = lte_lc_init_and_connect_async(lte_handler);
		if (err) {
			printk("Modem could not be configured, error: %d\n",
				err);
			return;
		}
	}
}
#endif /* IS_ENABLED(CONFIG_LTE_LINK_CONTROL) */

void main(void)
{
	int err;

	k_delayed_work_init(&direct_method_data.work, direct_method_handler);
	k_delayed_work_init(&twin_report_work, twin_report_work_fn);
	k_delayed_work_init(&send_event_work, send_event);
	cJSON_Init();

	printk("Azure IoT Hub sample started\n");

	err = azure_iot_hub_init(NULL, azure_event_handler);
	if (err) {
		printk("Azure IoT Hub could not be initialized, error: %d\n",
		       err);
		return;
	}

#if IS_ENABLED(CONFIG_LTE_LINK_CONTROL)
	printk("Connecting to LTE network\n");
	modem_configure();
	k_sem_take(&network_connected_sem, K_FOREVER);
#endif

	err = azure_iot_hub_connect();
	if (err < 0) {
		printk("azure_iot_hub_connect failed: %d\n", err);
		return;
	}
}
