/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <init.h>
#include <modem/at_cmd.h>

#include <memfault/core/build_info.h>
#include <memfault/core/compiler.h>
#include <memfault/core/math.h>
#include <memfault/core/platform/device_info.h>
#include <memfault/http/http_client.h>
#include <memfault/nrfconnect_port/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/metrics/metrics.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(memfault_integration, CONFIG_MEMFAULT_INTEGRATION_LOG_LEVEL);

#define IMEI_LEN 15

/* API key check */
BUILD_ASSERT(sizeof(CONFIG_MEMFAULT_API_KEY) > 1,
	"Memfault API Key not configured. Please visit https://go.memfault.com/create-key/nrf91");

/* Device ID check */
BUILD_ASSERT((sizeof(CONFIG_MEMFAULT_DEVICE_ID) > 1) || CONFIG_MEMFAULT_DEVICE_SERIAL_USE_IMEI,
	"Device ID must be set or condifured to use IMEI");

/* Firmware type check */
BUILD_ASSERT(sizeof(CONFIG_MEMFAULT_FW_TYPE) > 1, "Firmware type must be configured");

/* Firmware version checks */
#if defined(MEMFAULT_FW_VERSION_STATIC)
BUILD_ASSERT(sizeof(CONFIG_MEMFAULT_FW_VERSION_STATIC) > 1, "Firmware version must be configured");
#endif

static char fw_version[sizeof(CONFIG_MEMFAULT_FW_VERSION_PREFIX) + 8] =
	CONFIG_MEMFAULT_FW_VERSION_PREFIX;

#if defined(CONFIG_MEMFAULT_DEVICE_SERIAL_USE_IMEI)
static char device_serial[MAX(sizeof(CONFIG_MEMFAULT_DEVICE_ID), IMEI_LEN + 1)] =
	CONFIG_MEMFAULT_DEVICE_ID;
#else
static char device_serial[] = CONFIG_MEMFAULT_DEVICE_ID;
#endif

static struct k_work_delayable stack_check_work;

sMfltHttpClientConfig g_mflt_http_client_config = {
    .api_key = CONFIG_MEMFAULT_API_KEY,
};

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	static bool s_init = false;

	if (!s_init)
	{
		const size_t version_len = strlen(fw_version);
		// We will use 6 characters of the build id to make our versions unique and
		// identifiable between releases
		const size_t build_id_chars = 6 + 1 /* '\0' */;

		const size_t build_id_num_chars =
		    MIN(build_id_chars, sizeof(fw_version) - version_len - 1);

		memfault_build_id_get_string(&fw_version[version_len], build_id_num_chars);
		s_init = true;
	}

	// platform specific version information
	*info = (sMemfaultDeviceInfo){
		.device_serial = device_serial,
		.software_type = CONFIG_MEMFAULT_FW_TYPE,
		.software_version = fw_version,
		.hardware_version = CONFIG_MEMFAULT_HW_VERSION,
	};
}

static int request_imei(const char *cmd, char *buf, size_t buf_len)
{
	enum at_cmd_state at_state;
	int err = at_cmd_write(cmd, buf, buf_len, &at_state);

	if (err) {
		LOG_ERR("at_cmd_write failed, error: %d, at_state: %d", err, at_state);
	}

	return err;
}

static int device_info_init(void)
{
	int err;
	char imei_buf[IMEI_LEN + 2 + 1]; /* Add 2 for \r\n and 1 for \0 */

	err = request_imei("AT+CGSN", imei_buf, sizeof(imei_buf));
	if (err) {
		strncat(device_serial, "Unknown",
			sizeof(device_serial) - strlen(device_serial) - 1);
		LOG_ERR("Failed to retrieve IMEI");
	} else {
		imei_buf[IMEI_LEN] = '\0';
		strncat(device_serial, imei_buf,
			sizeof(device_serial) - strlen(device_serial) - 1);
	}

	LOG_DBG("Device serial generated: %s", log_strdup(device_serial));

	return err;
}

static void stack_check_cb(const struct k_thread *cthread, void *user_data)
{
	struct k_thread *thread = (struct k_thread *)cthread;
	char hexname[11];
	const char *name;
	size_t unused;
	int err;
	static size_t prev_unused;

	ARG_UNUSED(user_data);

	name = k_thread_name_get((k_tid_t)thread);
	if (!name || name[0] == '\0') {
		name = hexname;

		snprintk(hexname, sizeof(hexname), "%p", (void *)thread);
		LOG_DBG("No thread name registere for %s", name);
		return;
	}

	if (strncmp("at_cmd_socket_thread", name, sizeof("at_cmd_socket_thread")) != 0) {
		LOG_DBG("Not relevant stack: %s", name);
		return;
	}

	err = k_thread_stack_space_get(thread, &unused);
	if (err) {
		LOG_WRN(" %-20s: unable to get stack space (%d)", name, err);
		return;
	}

	if (unused == prev_unused) {
		return;
	}

	prev_unused = unused;

	LOG_DBG("Unused at_cmd stack size: %d", unused);

	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(at_cmd_free_stack_size), unused);
}

static void stack_check_work_fn(struct k_work *work)
{
	k_thread_foreach_unlocked(stack_check_cb, NULL);
	k_work_reschedule(k_work_delayable_from_work(work), K_SECONDS(600));
}

static int init(const struct device *unused)
{
	int err = 0;

	ARG_UNUSED(unused);

	k_work_init_delayable(&stack_check_work, stack_check_work_fn);

	if (IS_ENABLED(CONFIG_MEMFAULT_PROVISION_CERTIFICATES)) {
		err = memfault_zephyr_port_install_root_certs();
		if (err) {
			LOG_ERR("Failed to provision certificates, error: %d", err);
			LOG_WRN("Certificates can not be provisioned while LTE is active");
			/* We don't consider this a critical failure, as the application
			 * can attempt to provision at a later stage.
			 */
		}
	}

	if (IS_ENABLED(CONFIG_MEMFAULT_DEVICE_SERIAL_USE_IMEI)) {
		err = device_info_init();
		if (err) {
			LOG_ERR("Device info initialization failed, error: %d", err);
		}
	}

	k_work_schedule(&stack_check_work, K_NO_WAIT);


	return err;
}

SYS_INIT(init, APPLICATION, CONFIG_MEMFAULT_INIT_PRIORITY);
