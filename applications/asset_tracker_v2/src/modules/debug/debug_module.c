/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>

#if defined(CONFIG_MEMFAULT)
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault/ports/watchdog.h>
#include <memfault/panics/coredump.h>
#endif

#define MODULE debug_module

#if defined(CONFIG_WATCHDOG_APPLICATION)
#include "watchdog_app.h"
#endif /* CONFIG_WATCHDOG_APPLICATION */
#include "module_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DEBUG_MODULE_LOG_LEVEL);

/* Forward declarations. */
static int message_handler(struct module_msg *msg);
static struct module_data self = {
	.name = "debug",
	.msg_q = NULL,
	.supports_shutdown = false,
};
struct module_data *debug_module = &self;

#if defined(CONFIG_MEMFAULT)
/* Enumerator used to specify what type of Memfault that is sent. */
static enum memfault_data_type {
	METRICS,
	COREDUMP
} send_type;

static K_SEM_DEFINE(mflt_internal_send_sem, 0, 1);

static void memfault_internal_send(void)
{
	while (1) {
		k_sem_take(&mflt_internal_send_sem, K_FOREVER);

		if (send_type == COREDUMP) {
			if (memfault_coredump_has_valid_coredump(NULL)) {
				LOG_WRN("Sending a coredump to Memfault!");
			} else {
				LOG_DBG("No coredump available.");
				continue;
			}
		}

		/* If dispatching Memfault data over an external transport is not enabled,
		* Memfault SDKs internal HTTP transport is used.
		*/
#if !defined(CONFIG_DEBUG_MODULE_MEMFAULT_USE_EXTERNAL_TRANSPORT)
			memfault_zephyr_port_post_data();
#else
		uint8_t data[CONFIG_DEBUG_MODULE_MEMFAULT_CHUNK_SIZE_MAX];
		size_t len = sizeof(data);
		uint8_t *message = NULL;

		/* Retrieve, allocate and send parts of the memfault data.
		* After use it is expected that the data is freed.
		*/
		while (memfault_packetizer_get_chunk(data, &len)) {
			struct module_msg msg = {
				.type = DEBUG_MSG_MEMFAULT_DATA_READY,
			};

			message = k_malloc(len);
			if (message == NULL) {
				LOG_ERR("Failed to allocate memory for Memfault data");
				continue;
			}

			memcpy(message, data, len);

			msg.module.debug.memfault.len = len;
			msg.module.debug.memfault.buf = message;

			err = module_send_msg(cloud_module, &msg);
			if (err) {
				LOG_ERR("Failed to send Memfault message, error: %d", err);
			}

			len = sizeof(data);
		}
#endif
	}
}

K_THREAD_DEFINE(mflt_send_thread, CONFIG_DEBUG_MODULE_MEMFAULT_THREAD_STACK_SIZE,
		memfault_internal_send, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

#endif /* if defined(CONFIG_MEMFAULT) */

/* Handlers */

#if defined(CONFIG_MEMFAULT)
#if defined(CONFIG_WATCHDOG_APPLICATION)
static void watchdog_handler(const struct watchdog_evt *evt)
{
	int err;

	switch (evt->type) {
	case WATCHDOG_EVT_START:
		LOG_DBG("WATCHDOG_EVT_START");
		err = memfault_software_watchdog_enable();
		if (err) {
			LOG_ERR("memfault_software_watchdog_enable, error: %d", err);
		}
		break;
	case WATCHDOG_EVT_FEED:
		LOG_DBG("WATCHDOG_EVT_FEED");
		err = memfault_software_watchdog_feed();
		if (err) {
			LOG_ERR("memfault_software_watchdog_feed, error: %d", err);
		}
		break;
	case WATCHDOG_EVT_TIMEOUT_INSTALLED:
		LOG_DBG("WATCHDOG_EVT_TIMEOUT_INSTALLED");
		/* Set the software watchdog timeout slightly shorter than the actual watchdog
		 * timeout. This is to catch a timeout in advance so that Memfault can save
		 * coredump data before a reboot occurs.
		 */
		__ASSERT(evt->timeout > CONFIG_DEBUG_MODULE_MEMFAULT_WATCHDOG_DELTA_MS,
			 "Installed watchdog timeout is too small");

		err = memfault_software_watchdog_update_timeout(
						evt->timeout -
						CONFIG_DEBUG_MODULE_MEMFAULT_WATCHDOG_DELTA_MS);
		if (err) {
			LOG_ERR("memfault_software_watchdog_update_timeout, error: %d", err);
		}
		break;
	default:
		break;
	}
}
#endif /* defined(CONFIG_WATCHDOG_APPLICATION) */

/**
 * @brief Send Memfault data. To transfer Memfault data using an internal transport,
 *	  CONFIG_DEBUG_MODULE_MEMFAULT_USE_EXTERNAL_TRANSPORT must be selected.
 *	  Dispatching of Memfault data is offloaded to a dedicated thread.
 */
static void send_memfault_data(void)
{
	/* Offload sending of Memfault data to a dedicated thread. */
	if (memfault_packetizer_data_available()) {
		k_sem_give(&mflt_internal_send_sem);
	}
}

static void add_location_metrics(uint8_t satellites, uint32_t search_time,
				 enum module_msg_type event_type)
{
	int err;

	switch (event_type) {
	case LOCATION_MSG_GNSS_DATA_READY:
		err = memfault_metrics_heartbeat_set_unsigned(
						MEMFAULT_METRICS_KEY(GnssTimeToFix),
						search_time);
		if (err) {
			LOG_ERR("Failed updating GnssTimeToFix metric, error: %d", err);
		}
		break;
	case LOCATION_MSG_TIMEOUT:
		err = memfault_metrics_heartbeat_set_unsigned(
						MEMFAULT_METRICS_KEY(LocationTimeoutSearchTime),
						search_time);
		if (err) {
			LOG_ERR("Failed updating LocationTimeoutSearchTime metric, error: %d", err);
		}
		break;
	default:
		LOG_ERR("Unknown GNSS module messages type");
		return;
	}

	err = memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(GnssSatellitesTracked),
						      satellites);
	if (err) {
		LOG_ERR("Failed updating GnssSatellitesTracked metric, error: %d", err);
	}

	memfault_metrics_heartbeat_debug_trigger();
}

static void memfault_handle_event(struct module_msg *msg)
{
	if (IS_MSG(msg, APP_MSG_START)) {
		/* Register callback for watchdog events. Used to attach Memfault software
		 * watchdog.
		 */
#if defined(CONFIG_WATCHDOG_APPLICATION)
		watchdog_register_handler(watchdog_handler);
#endif
	}

	/* Send Memfault data at the same time application data is sent to save overhead
	 * compared to having Memfault SDK trigger regular updates independently. All data
	 * should preferably be sent within the same LTE RRC connected window.
	 */
	if ((IS_MSG(msg, DATA_MSG_DATA_SEND)) ||
	    (IS_MSG(msg, DATA_MSG_DATA_SEND_BATCH)) ||
	    (IS_MSG(msg, DATA_MSG_NEIGHBOR_CELLS_DATA_SEND)) ||
	    (IS_MSG(msg, DATA_MSG_UI_DATA_SEND))) {
		/* Limit how often non-coredump memfault data (events and metrics) are sent
		 * to memfault. Updates can never occur more often than the interval set by
		 * CONFIG_DEBUG_MODULE_MEMFAULT_UPDATES_MIN_INTERVAL_SEC and the first update is
		 * always sent.
		 */
		static int64_t last_update;

		if (((k_uptime_get() - last_update) <
		     (MSEC_PER_SEC * CONFIG_DEBUG_MODULE_MEMFAULT_UPDATES_MIN_INTERVAL_SEC)) &&
		    (last_update != 0)) {
			LOG_DBG("Not enough time has passed since the last Memfault update, abort");
			return;
		}

		last_update = k_uptime_get();
		send_type = METRICS;
		send_memfault_data();
		return;
	}

	/* If the module is configured to use Memfaults internal HTTP transport, coredumps are
	 * sent on an established connection to LTE.
	 */
	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTED) &&
	    !IS_ENABLED(CONFIG_DEBUG_MODULE_MEMFAULT_USE_EXTERNAL_TRANSPORT)) {
		/* Send coredump on LTE CONNECTED. */
		send_type = COREDUMP;
		send_memfault_data();
		return;
	}

	/* If the module is configured to use an external cloud transport, coredumps are
	 * sent on an established connection to the configured cloud service.
	 */
	if (IS_MSG(msg, CLOUD_MSG_CONNECTED) &&
	    IS_ENABLED(CONFIG_DEBUG_MODULE_MEMFAULT_USE_EXTERNAL_TRANSPORT)) {
		/* Send coredump on CLOUD CONNECTED. */
		send_type = COREDUMP;
		send_memfault_data();
		return;
	}

	if ((IS_MSG(msg, LOCATION_MSG_TIMEOUT)) ||
	    (IS_MSG(msg, LOCATION_MSG_GNSS_DATA_READY))) {
		add_location_metrics(msg->location.location.satellites_tracked,
				msg->location.location.search_time,
				msg->type);
		return;
	}
}
#endif /* defined(CONFIG_MEMFAULT) */

static int message_handler(struct module_msg *msg)
{
	if (IS_MSG(msg, APP_MSG_START)) {
		/* Notify the rest of the application that it is connected to network
		 * when building for QEMU x86.
		 */
		if (IS_ENABLED(CONFIG_BOARD_QEMU_X86)) {
			SEND_MSG(cloud_module, DEBUG_MSG_QEMU_X86_INITIALIZED);
			SEND_MSG(cloud_module, DEBUG_MSG_QEMU_X86_NETWORK_CONNECTED);
		}
	}

#if defined(CONFIG_MEMFAULT)
	memfault_handle_event(msg);
#endif

	return 0;
}

static int debug_module_start(const struct device *dev)
{
	ARG_UNUSED(dev);

	self.message_handler = message_handler;

	__ASSERT_NO_MSG(module_start(&self) == 0);

	return 0;
}

SYS_INIT(debug_module_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
