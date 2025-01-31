/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/watchdog.h>
#include <sys/reboot.h>
#include <task_wdt/task_wdt.h>
#include <logging/log.h>

#include "slm_util.h"
#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog, CONFIG_SLM_LOG_LEVEL);

/* Create a delayed work that continuously feeds the watchdog on the system work queue.
 * The system work queue is also used for sending AT commands to the modem, so a watchdog
 * timeout will be triggered if the modem is unresponsive.
 */
static void feed_watchdog_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(feed_watchdog_work, feed_watchdog_work_fn);
static int sys_workqueue_wd_channel_id;

static void sys_workqueue_watchdog_callback(int channel_id, void *user_data)
{
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	LOG_ERR("System workqueue watchdog");

	slm_util_reboot(3);
}

static void feed_watchdog_work_fn(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	LOG_DBG("Feeding watchdog");

	err = task_wdt_feed(sys_workqueue_wd_channel_id);
	if (err) {
		LOG_ERR("task_wdt_feed() error: %d", err);
	}

	/* Feed the watchdog */
	k_work_reschedule(&feed_watchdog_work,
			  K_MSEC(WATCHDOG_SYSTEM_WORKQUEUE_FEED_INTERVAL_MSEC));
}

static int init_task_watchdog(const struct device *unused)
{
	int err;

	ARG_UNUSED(unused);

	const struct device *dev = device_get_binding(DT_LABEL(DT_NODELABEL(wdt)));
	if (!dev) {
		LOG_ERR("WDT not available");
	}

	err = task_wdt_init(dev);

	if (err) {
		LOG_ERR("task_wdt_init() error: %d", err);
	}

	/* Initialize and start feeding the watchdog on the system workqueue */
	sys_workqueue_wd_channel_id =
		task_wdt_add(CONFIG_SLM_WATCHDOG_SYSTEM_WORKQUEUE_TIMEOUT_MSEC,
			     sys_workqueue_watchdog_callback, NULL);
	if (sys_workqueue_wd_channel_id < 0) {
		LOG_ERR("task_wdt_add() error: %d", sys_workqueue_wd_channel_id);
		return -ENXIO;
	}

	k_work_reschedule(&feed_watchdog_work, K_NO_WAIT);

	return 0;
}

SYS_INIT(init_task_watchdog, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
