/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <conn_bridge_control/control_host.h>

int main(void)
{
	printk("Connectivity Bridge Control Host\n");
	
	const struct device *eeprom = DEVICE_DT_GET(DT_NODELABEL(virtual_eeprom));
	if (!device_is_ready(eeprom)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       eeprom->name);
		return -1;
	}

	conn_bridge_control_host_init(eeprom);

	bool attached = false;
	while(true) {
		k_msleep(700);
		conn_bridge_control_set_usb_attached(attached);
		attached = !attached;
	}
	return 0;
}
