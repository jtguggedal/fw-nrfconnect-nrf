/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_MEMFAULT_DEVICE_ID_USE_IMEI
#include <modem/at_cmd.h>
#include <init.h>
#endif

#include "memfault/core/build_info.h"
#include "memfault/core/compiler.h"
#include "memfault/core/math.h"
#include "memfault/core/platform/device_info.h"
#include "memfault/http/http_client.h"
#include "memfault/nrfconnect_port/http.h"

#define IMEI_LEN 15

static char s_fw_version[20] = "v0.0.1+";
static char s_device_serial[MAX(sizeof(CONFIG_MEMFAULT_DEVICE_ID), IMEI_LEN + 1)] =
	CONFIG_MEMFAULT_DEVICE_ID;

BUILD_ASSERT(sizeof(CONFIG_MEMFAULT_API_KEY) > 2, "API key must be configured");
BUILD_ASSERT((sizeof(CONFIG_MEMFAULT_DEVICE_ID) > 2) || CONFIG_MEMFAULT_DEVICE_ID_USE_IMEI,
	"Device ID must be set or condifured to use IMEI");

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	static bool s_init = false;

	if (!s_init)
	{
		const size_t version_len = strlen(s_fw_version);
		// We will use 6 characters of the build id to make our versions unique and
		// identifiable between releases
		const size_t build_id_chars = 6 + 1 /* '\0' */;

		const size_t build_id_num_chars =
		    MIN(build_id_chars, sizeof(s_fw_version) - version_len - 1);

		memfault_build_id_get_string(&s_fw_version[version_len], build_id_num_chars);
		s_init = true;
	}

	// platform specific version information
	*info = (sMemfaultDeviceInfo){
		.device_serial = s_device_serial,
		.software_type = "nrf91ns-fw",
		.software_version = s_fw_version,
		.hardware_version = "proto",
	};
}

sMfltHttpClientConfig g_mflt_http_client_config = {
    .api_key = CONFIG_MEMFAULT_API_KEY,
};

#ifdef CONFIG_MEMFAULT_DEVICE_ID_USE_IMEI

static int query_modem(const char *cmd, char *buf, size_t buf_len)
{
	enum at_cmd_state at_state;
	int ret = at_cmd_write(cmd, buf, buf_len, &at_state);

	if (ret != 0)
	{
		printk("at_cmd_write [%s] error:%d, at_state: %d",
		       cmd, ret, at_state);
	}

	return ret;
}

static int prv_init_device_info(const struct device *unused)
{
	ARG_UNUSED(unused);

	char imei_buf[IMEI_LEN + 2 /* for \r\n */ + 1 /* \0 */];
	if (query_modem("AT+CGSN", imei_buf, sizeof(imei_buf)) != 0)
	{
		strcat(s_device_serial, "Unknown");
		return 0;
	}

	imei_buf[IMEI_LEN] = '\0';
	strcat(s_device_serial, imei_buf);

	return 0;
}

SYS_INIT(prv_init_device_info, APPLICATION, CONFIG_MEMFAULT_INIT_PRIORITY);

#endif /* CONFIG_MEMFAULT_DEVICE_ID_USE_IMEI */
