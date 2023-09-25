/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/virtual_eeprom.h>

void eeprom_evt_handler(enum virtual_eeprom_evt event, uint16_t addr, uint16_t len) {
	switch(event) {
		case VIRTUAL_EEPROM_READ:
			printk("Read %x bytes from %x\n", len, addr);
			break;
		case VIRTUAL_EEPROM_WRITE:
			printk("Wrote %x bytes to %x\n", len, addr);
			break;
	}
}

int main(void)
{
	printk("Virtual EEPROM sample\n");
	
	const struct device *eeprom = DEVICE_DT_GET(DT_NODELABEL(virtual_eeprom));
	if (!device_is_ready(eeprom)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       eeprom->name);
		return -1;
	}
	printk("Found virtual EEPROM device \"%s\"\n", eeprom->name);

	virtual_eeprom_set_evt_handler(eeprom, eeprom_evt_handler);

	uint32_t data = 0xCAFEBABE;
	virtual_eeprom_write(eeprom, 0x0, &data, sizeof(data));
	return 0;
}
