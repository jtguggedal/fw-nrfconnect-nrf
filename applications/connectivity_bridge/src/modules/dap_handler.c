/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr/usb/usb_device.h>
#include <zephyr/kernel.h>

#include <cmsis_dap.h>
#include "cmsis_dap_usb.h"

#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>

#define ID_DAP_VENDOR14 (ID_DAP_VENDOR0 + 14)
#define ID_DAP_VENDOR_BOOTLOADER ID_DAP_VENDOR14

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_handler, CONFIG_DAP_LOG_LEVEL);

static int dap_vendor_handler(const uint8_t *request, uint8_t *response)
{
	if (*request == ID_DAP_VENDOR_BOOTLOADER) {
		bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
		sys_reboot(SYS_REBOOT_WARM);
		/* no return from here */
	}
	response[0] = ID_DAP_INVALID;
	return 1U;
}

static int dap_handler_loop(void)
{
	int ret;

	const struct device *const swd_dev = DEVICE_DT_GET_ONE(zephyr_swdp_gpio);

	(void) dap_install_vendor_callback(dap_vendor_handler);
	
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
