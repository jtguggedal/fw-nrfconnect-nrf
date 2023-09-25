/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <conn_bridge_control/control_client.h>


int main(void)
{
	int err;

	printk("Connectivity Bridge Control Client\n");
	
	const struct device *eeprom = DEVICE_DT_GET(DT_NODELABEL(eeprom));
	if (!device_is_ready(eeprom)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       eeprom->name);
		return -1;
	}
	conn_bridge_control_client_init(eeprom);
	
	bool attached = false;
	while(true) {
		k_msleep(1000);
		err = conn_bridge_control_get_usb_attached(&attached);
		if(err) {
			printk("Error while fetching attached status: %d\n", err);
			continue;
		}
		printk("USB attached: %d\n", attached);
	}

	return 0;
}
