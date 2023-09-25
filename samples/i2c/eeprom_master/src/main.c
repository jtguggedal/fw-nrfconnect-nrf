/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/eeprom.h>
#include <zephyr/device.h>

#include <zephyr/kernel.h>



int main(void)
{
	int err;

	printk("EEPROM MASTER\n");
	
	const struct device *eeprom = DEVICE_DT_GET(DT_NODELABEL(eeprom));
	if (!device_is_ready(eeprom)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       eeprom->name);
		return -1;
	}
	printk("Found EEPROM device \"%s\"\n", eeprom->name);

	size_t eeprom_size = eeprom_get_size(eeprom);
	printk("EEPROM has size %zu\n", eeprom_size);

	size_t read_data;
	err = eeprom_read(eeprom, 0x0, &read_data, sizeof(read_data));
	printk("EEPROM data %lx\n", read_data);

	while(true) {
		k_msleep(5000);
		size_t write_data = 0xdeadbeef;
		err = eeprom_write(eeprom, 0x0, &write_data, sizeof(write_data));
		err = eeprom_read(eeprom, 0x0, &read_data, sizeof(read_data));
		printk("EEPROM data %lx\n", read_data);

	}
	return 0;
}
