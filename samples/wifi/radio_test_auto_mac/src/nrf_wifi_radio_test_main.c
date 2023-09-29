/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include "ficr_prog.h"
#include <util.h>
#include <nrf_wifi_radio_test_shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/rand32.h>

int main(void)
{
	unsigned int val[OTP_MAX_WORD_LEN];
	unsigned int write_val[2];
	unsigned int ret, err;

	printk("reading OTP...\n");
	err = read_otp_memory(0, &val[0], OTP_MAX_WORD_LEN);
	if (err) {
		printk("FAILED reading otp memory......\n");
		return -ENOEXEC;
	}
	ret = check_protection(&val[0], REGION_PROTECT + 0, REGION_PROTECT + 1,
							REGION_PROTECT + 2, REGION_PROTECT + 3);
	if (ret == OTP_FRESH_FROM_FAB) {
		printk("writing REGION_PROTECT...\n");
		write_val[0] = 0x50FA50FA;
		ret = write_otp_memory(REGION_PROTECT, &write_val[0]);
		if (ret) {
			printk("unable to write REGION_PROTECT!\n");
			return -ENOEXEC;
		}
		sys_reboot(SYS_REBOOT_COLD);
	}

	if (val[REGION_DEFAULTS] & (~MAC0_ADDR_FLAG_MASK)) {
		if (val[MAC0_ADDR] == 0xFFFFFFFF) {
			val[MAC0_ADDR] = 0x0036CEF4 | (sys_rand32_get() & 0xFF000000);
			val[MAC0_ADDR+1] = sys_rand32_get() & 0xFFFF;
		}
		printk("writing MAC address 1\n");
		ret = write_otp_memory(MAC0_ADDR, &val[MAC0_ADDR]);
		if (ret) {
			printk("unable to write MAC0_ADDR!\n");
			return -ENOEXEC;
		}
	}

	if (val[REGION_DEFAULTS] & (~MAC1_ADDR_FLAG_MASK)) {
		if (val[MAC1_ADDR] == 0xFFFFFFFF) {
			val[MAC1_ADDR] = 0x0036CEF4 | (sys_rand32_get() & 0xFF000000);
			val[MAC1_ADDR+1] = sys_rand32_get() & 0xFFFF;
		}
		printk("writing MAC address 1\n");
		ret = write_otp_memory(MAC1_ADDR, &val[MAC1_ADDR]);
		if (ret) {
			printk("unable to write MAC1_ADDR!\n");
			return -ENOEXEC;
		}
	}

	if (val[REGION_DEFAULTS] & (~CALIB_XO_FLAG_MASK)) {
		printk("writing CALIB_XO\n");
		write_val[0] = 0x30;
		ret = write_otp_memory(CALIB_XO, &write_val[0]);
		if (ret) {
			printk("unable to write CALIB_XO!\n");
			return -ENOEXEC;
		}
	}

	return 0;
}
