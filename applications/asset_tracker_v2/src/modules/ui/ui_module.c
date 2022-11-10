/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <dk_buttons_and_leds.h>

#define MODULE ui_module

#include "module_common.h"
#include "led_state_def.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_UI_MODULE_LOG_LEVEL);

/* Define a custom STATIC macro that exposes internal variables when unit testing. */
#if defined(CONFIG_UNITY)
#define STATIC
#else
#define STATIC static
#endif

/* UI module states. */
STATIC enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_LTE_CONNECTING,
	STATE_CLOUD_CONNECTING,
	STATE_CLOUD_ASSOCIATING,
	STATE_FOTA_UPDATING,
	STATE_SHUTDOWN
} state;

/* UI module sub states. */
STATIC enum sub_state_type {
	SUB_STATE_ACTIVE,
	SUB_STATE_PASSIVE,
} sub_state;

/* UI module sub-sub states. */
STATIC enum sub_sub_state_type {
	SUB_SUB_STATE_LOCATION_INACTIVE,
	SUB_SUB_STATE_LOCATION_ACTIVE
} sub_sub_state;


/* Forward declarations */
static void led_pattern_update_work_fn(struct k_work *work);

/* Definition used to specify LED patterns that should hold forever. */
#define HOLD_FOREVER -1

/* List of LED patterns supported in the UI module. */
STATIC struct led_pattern {
	/* Variable used to construct a linked list of led patterns. */
	sys_snode_t header;
	/* LED state. */
	enum led_state led_state;
	/* Duration of the LED state. */
	int16_t duration_sec;
} led_pattern_list[LED_STATE_COUNT];

/* Linked list used to schedule multiple LED pattern transitions. */
STATIC sys_slist_t pattern_transition_list = SYS_SLIST_STATIC_INIT(&pattern_transition_list);

/* Delayed work that is used to display and transition to the correct LED pattern depending on the
 * internal state of the module.
 */
static K_WORK_DELAYABLE_DEFINE(led_pattern_update_work, led_pattern_update_work_fn);

/* UI module message queue. */
#define UI_QUEUE_ENTRY_COUNT		10
#define UI_QUEUE_BYTE_ALIGNMENT		4

static struct module_data self = {
	.name = "ui",
	.msg_q = NULL,
	.supports_shutdown = true,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *ui_module = &self;

/* Forward declarations. */
static int message_handler(struct module_msg *msg);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_LTE_CONNECTING:
		return "STATE_LTE_CONNECTING";
	case STATE_CLOUD_CONNECTING:
		return "STATE_CLOUD_CONNECTING";
	case STATE_CLOUD_ASSOCIATING:
		return "STATE_CLOUD_ASSOCIATING";
	case STATE_FOTA_UPDATING:
		return "STATE_FOTA_UPDATING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

/* Convenience functions used in internal state handling. */
static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_ACTIVE:
		return "SUB_STATE_ACTIVE";
	case SUB_STATE_PASSIVE:
		return "SUB_STATE_PASSIVE";
	default:
		return "Unknown";
	}
}

static char *sub_sub_state2str(enum sub_sub_state_type new_state)
{
	switch (new_state) {
	case SUB_SUB_STATE_LOCATION_INACTIVE:
		return "SUB_SUB_STATE_LOCATION_INACTIVE";
	case SUB_SUB_STATE_LOCATION_ACTIVE:
		return "SUB_SUB_STATE_LOCATION_ACTIVE";
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

static void sub_sub_state_set(enum sub_sub_state_type new_state)
{
	if (new_state == sub_sub_state) {
		LOG_DBG("Sub state: %s", sub_sub_state2str(sub_sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_sub_state2str(sub_sub_state),
		sub_sub_state2str(new_state));

	sub_sub_state = new_state;
}

/* Handlers */

static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	if (has_changed & button_states & DK_BTN1_MSK) {
		int err;
		struct module_msg msg = {
			.type = UI_MSG_BUTTON_DATA_READY,
			.ui.btn.button_number = 1,
			.ui.btn.timestamp = k_uptime_get(),
		};

		err = module_send_msg(data_module, &msg);
		if (err) {
			LOG_ERR("Failed to send button data, error: %d", err);
		}

		err = module_send_msg(app_module, &msg);
		if (err) {
			LOG_ERR("Failed to send button data, error: %d", err);
		}
	}

#if defined(CONFIG_BOARD_NRF9160DK_NRF9160_NS)
	if (has_changed & button_states & DK_BTN2_MSK) {
		int err;
		struct module_msg msg = {
			.type = UI_MSG_BUTTON_DATA_READY,
			.ui.btn.button_number = 2,
			.ui.btn.timestamp = k_uptime_get(),
		};

		err = module_send_msg_all(&msg);
		if (err) {
			LOG_ERR("Failed to send button data, error: %d", err);
		}

	}
#endif
}

/* Static module functions. */
static void update_led_pattern(enum led_state pattern)
{
#if defined(CONFIG_LED_CONTROL)
	BUILD_ASSERT(false, "NOT IMPLEMENTED");

	struct led_state_event *event = new_led_state_event();

	__ASSERT(event, "Not enough heap left to allocate event");

	event->state = pattern;
	APP_EVENT_SUBMIT(event);
#endif
}

static void led_pattern_update_work_fn(struct k_work *work)
{
	struct led_pattern *next_pattern;
	static enum led_state previous_led_state = LED_STATE_COUNT;
	sys_snode_t *node = sys_slist_get(&pattern_transition_list);

	if (node == NULL) {
		LOG_ERR("Cannot find any more LED pattern transitions");
		return;
	}

	next_pattern = CONTAINER_OF(node, struct led_pattern, header);

	/* Prevent the same LED led_state from being scheduled twice in a row. */
	if (next_pattern->led_state != previous_led_state) {
		update_led_pattern(next_pattern->led_state);
		previous_led_state = next_pattern->led_state;
	}

	/* Even if the LED state is not updated due a match with the previous state a LED pattern
	 * update is scheduled. This will prolong the pattern until the LED pattern transition
	 * list is cleared.
	 */
	if (next_pattern->duration_sec > 0) {
		k_work_reschedule(&led_pattern_update_work, K_SECONDS(next_pattern->duration_sec));
	}
}

/* Function that checks if incoming event causes cloud activity. */
static bool is_cloud_related_event(struct module_msg *msg)
{
	if ((IS_MSG(msg, DATA_MSG_DATA_SEND)) ||
	    (IS_MSG(msg, CLOUD_MSG_CONNECTED)) ||
	    (IS_MSG(msg, DATA_MSG_UI_DATA_SEND)) ||
	    (IS_MSG(msg, DATA_MSG_DATA_SEND_BATCH)) ||
	    (IS_MSG(msg, DATA_MSG_NEIGHBOR_CELLS_DATA_SEND))) {
		return true;
	}

	return false;
}

/* Function that clears LED pattern transition list. */
STATIC void transition_list_clear(void)
{
	struct led_pattern *transition, *next_transition = NULL;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&pattern_transition_list,
					  transition,
					  next_transition,
					  header) {
		sys_slist_remove(&pattern_transition_list, NULL, &transition->header);
	};
}

/* Function that appends a LED state and a corresponding duration to the
 * LED pattern transition list.
 */
static void transition_list_append(enum led_state led_state, int16_t duration_sec)
{
	led_pattern_list[led_state].led_state = led_state;
	led_pattern_list[led_state].duration_sec = duration_sec;

	sys_slist_append(&pattern_transition_list, &led_pattern_list[led_state].header);
}

/* Message handler for SUB_SUB_STATE_LOCATION_ACTIVE in SUB_STATE_ACTIVE. */
static void on_active_location_active(struct module_msg *msg)
{
	if (is_cloud_related_event(msg)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_PUBLISHING, 5);
		transition_list_append(LED_STATE_ACTIVE_MODE, 5);
		transition_list_append(LED_STATE_LOCATION_SEARCHING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}
}

/* Message handler for SUB_SUB_STATE_LOCATION_INACTIVE in SUB_STATE_ACTIVE. */
static void on_active_location_inactive(struct module_msg *msg)
{
	if (is_cloud_related_event(msg)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_PUBLISHING, 5);
		transition_list_append(LED_STATE_ACTIVE_MODE, 5);
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}
}

/* Message handler for SUB_SUB_STATE_LOCATION_ACTIVE in SUB_STATE_PASSIVE. */
static void on_passive_location_active(struct module_msg *msg)
{
	if (is_cloud_related_event(msg)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_PUBLISHING, 5);
		transition_list_append(LED_STATE_PASSIVE_MODE, 5);
		transition_list_append(LED_STATE_LOCATION_SEARCHING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}
}

/* Message handler for SUB_SUB_STATE_LOCATION_INACTIVE in SUB_STATE_PASSIVE. */
static void on_passive_location_inactive(struct module_msg *msg)
{
	if (is_cloud_related_event(msg)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_PUBLISHING, 5);
		transition_list_append(LED_STATE_PASSIVE_MODE, 5);
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct module_msg *msg)
{
	if (IS_MSG(msg, APP_MSG_START)) {
		state_set(STATE_RUNNING);
		sub_state_set(SUB_STATE_ACTIVE);
		sub_sub_state_set(SUB_SUB_STATE_LOCATION_INACTIVE);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct module_msg *msg)
{
	if (IS_MSG(msg, LOCATION_MSG_ACTIVE)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LOCATION_SEARCHING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}

	if (IS_MSG(msg, LOCATION_MSG_INACTIVE)) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}

}

/* Message handler for STATE_LTE_CONNECTING. */
static void on_state_lte_connecting(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTED)) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_CLOUD_CONNECTING. */
static void on_state_cloud_connecting(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_CONNECTED)) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}

	if (IS_MSG(msg, CLOUD_MSG_USER_ASSOCIATED)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_ASSOCIATED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_CLOUD_ASSOCIATING. */
static void on_state_cloud_associating(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_USER_ASSOCIATED)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_ASSOCIATED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_FOTA_UPDATING. */
static void on_state_fota_update(struct module_msg *msg)
{
	if ((IS_MSG(msg, CLOUD_MSG_FOTA_DONE)) ||
	    (IS_MSG(msg, CLOUD_MSG_FOTA_ERROR))) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for all states. */
static void on_all_states(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTING)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LTE_CONNECTING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_LTE_CONNECTING);
	}

	if (IS_MSG(msg, CLOUD_MSG_CONNECTING)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_CONNECTING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_CLOUD_CONNECTING);
	}

	if (IS_MSG(msg, UTIL_MSG_SHUTDOWN_REQUEST)) {

		transition_list_clear();

		switch (msg->util.reason) {
		case REASON_FOTA_UPDATE:
			transition_list_append(LED_STATE_FOTA_UPDATE_REBOOT, HOLD_FOREVER);
			break;
		case REASON_GENERIC:
			transition_list_append(LED_STATE_ERROR_SYSTEM_FAULT, HOLD_FOREVER);
			break;
		default:
			LOG_ERR("Unknown shutdown reason");
			break;
		}

		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);

		SEND_SHUTDOWN_ACK(UI_MSG_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if ((IS_MSG(msg, DATA_MSG_CONFIG_INIT)) ||
	    (IS_MSG(msg, DATA_MSG_CONFIG_READY))) {
		sub_state_set(msg->data.cfg.active_mode ?
			      SUB_STATE_ACTIVE :
			      SUB_STATE_PASSIVE);
	}

	if (IS_MSG(msg, LOCATION_MSG_ACTIVE)) {
		sub_sub_state_set(SUB_SUB_STATE_LOCATION_ACTIVE);
	}

	if (IS_MSG(msg, LOCATION_MSG_INACTIVE)) {
		sub_sub_state_set(SUB_SUB_STATE_LOCATION_INACTIVE);
	}

	if (IS_MSG(msg, CLOUD_MSG_FOTA_START)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_UPDATING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_FOTA_UPDATING);
	}

	if (IS_MSG(msg, CLOUD_MSG_USER_ASSOCIATION_REQUEST)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_ASSOCIATING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_CLOUD_ASSOCIATING);
	}
}

static int message_handler(struct module_msg *msg)
{
	switch (state) {
	case STATE_INIT:
		on_state_init(msg);
		break;
	case STATE_RUNNING:
		switch (sub_state) {
		case SUB_STATE_ACTIVE:
			switch (sub_sub_state) {
			case SUB_SUB_STATE_LOCATION_ACTIVE:
				on_active_location_active(msg);
				break;
			case SUB_SUB_STATE_LOCATION_INACTIVE:
				on_active_location_inactive(msg);
				break;
			default:
				LOG_ERR("Unknown sub-sub state.");
				break;
			}
			break;
		case SUB_STATE_PASSIVE:
			switch (sub_sub_state) {
			case SUB_SUB_STATE_LOCATION_ACTIVE:
				on_passive_location_active(msg);
				break;
			case SUB_SUB_STATE_LOCATION_INACTIVE:
				on_passive_location_inactive(msg);
				break;
			default:
				LOG_ERR("Unknown sub-sub state.");
				break;
			}
			break;
		default:
			LOG_ERR("Unknown sub state.");
			break;
		}
		on_state_running(msg);
		break;
	case STATE_LTE_CONNECTING:
		on_state_lte_connecting(msg);
		break;
	case STATE_CLOUD_CONNECTING:
		on_state_cloud_connecting(msg);
		break;
	case STATE_CLOUD_ASSOCIATING:
		on_state_cloud_associating(msg);
		break;
	case STATE_FOTA_UPDATING:
		on_state_fota_update(msg);
		break;
	case STATE_SHUTDOWN:
		/* The shutdown state has no transition. */
		break;
	default:
		LOG_ERR("Unknown state.");
		break;
	}

	on_all_states(msg);

	return 0;
}

static int ui_module_start(const struct device *dev)
{
	ARG_UNUSED(dev);

	int err;

	self.message_handler = message_handler;

	__ASSERT_NO_MSG(module_start(&self) == 0);

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("dk_buttons_init, error: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(ui_module_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
