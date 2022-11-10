/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#define MODULE util_module

#if defined(CONFIG_WATCHDOG_APPLICATION)
#include "watchdog_app.h"
#endif
#include "module_common.h"

LOG_MODULE_REGISTER(MODULE, CONFIG_UTIL_MODULE_LOG_LEVEL);

/* Util module super states. */
static enum state_type {
	STATE_INIT,
	STATE_REBOOT_PENDING
} state;

/* Forward declarations. */
static void reboot_work_fn(struct k_work *work);
static int message_handler(struct module_msg *msg);
static void send_reboot_request(enum shutdown_reason reason);

/* Delayed work that is used to trigger a reboot. */
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_fn);

static struct module_data self = {
	.name = "util",
	.msg_q = NULL,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *util_module = &self;

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_REBOOT_PENDING:
		return "STATE_REBOOT_PENDING";
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

/* Handlers */

void bsd_recoverable_error_handler(uint32_t err)
{
	send_reboot_request(REASON_GENERIC);
}

/* Static module functions. */

static void reboot_work_fn(struct k_work *work)
{
	LOG_ERR("Rebooting!");
#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else
	while (true) {
		k_cpu_idle();
	}
#endif
}

static void send_reboot_request(enum shutdown_reason reason)
{
	/* Flag ensuring that multiple shutdown requests are not emitted
	 * upon an error from multiple modules.
	 */
	static bool shutdown_requested;

	if (!shutdown_requested) {
		struct module_msg msg = {
			.type = UTIL_MSG_SHUTDOWN_REQUEST,
			.util.reason = reason,
		};

		k_work_reschedule(&reboot_work, K_SECONDS(CONFIG_REBOOT_TIMEOUT));
		(void)module_send_msg_all(&msg);
		state_set(STATE_REBOOT_PENDING);

		shutdown_requested = true;
	}
}

/* This API should be called exactly once for each _SHUTDOWN_READY event received from active
 * modules in the application. When this API has been called a set number of times equal to the
 * number of active modules, a reboot will be scheduled.
 */
static void reboot_ack_check(uint32_t module_id)
{
	/* Reboot after a shorter timeout if all modules have acknowledged that they are ready
	 * to reboot, ensuring a graceful shutdown. If not all modules respond to the shutdown
	 * request, the application will be shut down after a longer duration scheduled upon the
	 * initial error event.
	 */
	if (modules_shutdown_register(module_id)) {
		LOG_WRN("All modules have ACKed the reboot request.");
		LOG_WRN("Reboot in 5 seconds.");
		k_work_reschedule(&reboot_work, K_SECONDS(5));
	}
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_FOTA_DONE)) {
		send_reboot_request(REASON_FOTA_UPDATE);
	}

	if ((IS_MSG(msg, CLOUD_MSG_ERROR))			||
	    (IS_MSG(msg, MODEM_MSG_ERROR))			||
	    (IS_MSG(msg, SENSOR_MSG_ERROR))			||
	    (IS_MSG(msg, LOCATION_MSG_ERROR_CODE))		||
	    (IS_MSG(msg, DATA_MSG_ERROR))			||
	    (IS_MSG(msg, APP_MSG_ERROR))			||
	    (IS_MSG(msg, UI_MSG_ERROR))				||
	    (IS_MSG(msg, MODEM_MSG_CARRIER_REBOOT_REQUEST))	||
	    (IS_MSG(msg, CLOUD_MSG_REBOOT_REQUEST))) {
		send_reboot_request(REASON_GENERIC);
		return;
	}
}

/* Message handler for STATE_REBOOT_PENDING. */
static void on_state_reboot_pending(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->cloud.id);
		return;
	}

	if (IS_MSG(msg, MODEM_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->modem.id);
		return;
	}

	if (IS_MSG(msg, SENSOR_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->sensor.id);
		return;
	}

	if (IS_MSG(msg, LOCATION_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->location.id);
		return;
	}

	if (IS_MSG(msg, DATA_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->data.id);
		return;
	}

	if (IS_MSG(msg, APP_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->app.id);
		return;
	}

	if (IS_MSG(msg, UI_MSG_SHUTDOWN_READY)) {
		reboot_ack_check(msg->ui.id);
		return;
	}
}

/* Message handler for all states. */
static void on_all_states(struct module_msg *msg)
{
	if (IS_MSG(msg, APP_MSG_START)) {
		state_set(STATE_INIT);
	}
}

static int message_handler(struct module_msg *msg)
{
	switch (state) {
	case STATE_INIT:
		on_state_init(msg);
		break;
	case STATE_REBOOT_PENDING:
		on_state_reboot_pending(msg);
		break;
	default:
		LOG_WRN("Unknown utility module state.");
		break;
	}

	on_all_states(msg);

	return 0;
}

static int util_module_start(const struct device *dev)
{
	ARG_UNUSED(dev);

	self.message_handler = message_handler;

	__ASSERT_NO_MSG(module_start(&self) == 0);

#if defined(CONFIG_WATCHDOG_APPLICATION)
	int err = watchdog_init_and_start();

	if (err) {
		LOG_DBG("watchdog_init_and_start, error: %d", err);
		send_reboot_request(REASON_GENERIC);
	}
#endif

	return 0;
}

SYS_INIT(util_module_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
