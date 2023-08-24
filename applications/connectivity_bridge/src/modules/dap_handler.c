/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr/usb/usb_device.h>
#include <zephyr/kernel.h>

#include "cmsis_dap_usb.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_handler, CONFIG_DAP_LOG_LEVEL);

static int dap_handler_loop(void)
{
	int ret;

	const struct device *const swd_dev = DEVICE_DT_GET_ONE(zephyr_swdp_gpio);
	
	ret = cmsis_dap_usb_init(swd_dev);
	if (ret != 0) {
		LOG_ERR("Failed to init CMSIS-DAP");
		return 0;
	}

	while (true)
	{
		cmsis_dap_usb_process(K_FOREVER);
	}
	return 0;
}

K_THREAD_DEFINE(dap_handler_thread, CONFIG_DAP_HANDLER_STACK_SIZE,
		dap_handler_loop, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
