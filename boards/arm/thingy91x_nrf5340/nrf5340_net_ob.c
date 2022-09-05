/*
 * Copyright (c) 2023 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/sys/util_macro.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <soc.h>

LOG_MODULE_REGISTER(thingy91x_nrf5340_cpunet, CONFIG_LOG_DEFAULT_LEVEL);

uint32_t *app_ready = (uint32_t *)0x20070000;
uint32_t *net_ready = (uint32_t *)0x20070024;

int greet_app_core(void) {
	for (size_t i = 0; i < 10000; ++i) {
		if (*app_ready == 0x11223344) {
			break;
		}
	}
	*net_ready = 1;
	return 0;
}

SYS_INIT(greet_app_core, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_OBJECTS);
