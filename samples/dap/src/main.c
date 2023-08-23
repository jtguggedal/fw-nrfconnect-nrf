/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sample app for implementing a CMSIS-DAP debug probe.
 *
 * Sample app for implementing a CMSIS-DAP debug probe.
 * This should be usable by systems that support CMSIS-DAP probes.
 * It includes the necessary Windows USB descriptors to be installed automatically on
 * Windows 8.1 and newer.
 */


#include <zephyr/usb/usb_device.h>

#include "cmsis_dap_usb.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

int main(void)
{
	int ret;

	const struct device *const swd_dev = DEVICE_DT_GET_ONE(zephyr_swdp_gpio);
	
	ret = cmsis_dap_usb_init(swd_dev);
	if (ret != 0) {
		LOG_ERR("Failed to init CMSIS-DAP");
		return 0;
	}

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	while (true)
	{
		cmsis_dap_usb_process(K_FOREVER);
	}
	return 0;
}
