/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <init.h>
#include <nrf_modem_at.h>
#include <modem/nrf_modem_lib.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(board_nonsecure, CONFIG_BOARD_LOG_LEVEL);

#define AT_CMD_MAX_READ_LENGTH	128
#define AT_CMD_LEN(cmd)		(sizeof (cmd) - 1)
#define AT_CMD_MAGPIO		"AT%%XMAGPIO=1,1,1,7,1,746,803,2,698,748," \
				"2,1710,2200,3,824,894,4,880,960,5,791,849," \
				"7,1565,1586"
#define AT_CMD_COEX0		"AT%%XCOEX0=1,1,1565,1586"
#define AT_CMD_TRACE		"AT%%XMODEMTRACE=0"

extern void nrf_modem_lib_on_init_done(void);

static int thingy91_magpio_configure(void)
{
#if defined(CONFIG_NRF_MODEM_LIB) && defined(CONFIG_NET_SOCKETS_OFFLOAD)
	int err;

	err = nrf_modem_lib_get_init_ret();
	if (err < 0) {
		LOG_ERR("nrf_modem_lib_get_init_ret failed, error: %d", err);
		return err;
	} else if (err > 0) {
		LOG_WRN("A modem firmware upgrade has been performed, reboot is expected.");

		/** If a modem firmware upgrade has been performed, calls to nrf_modem_at_
		 *  are expected to fail until a reboot has been carried out.
		 */
		return err;
	}

	LOG_DBG("AT CMD: %s", log_strdup(AT_CMD_TRACE));
	err = nrf_modem_at_printf(AT_CMD_TRACE);
	if (err) {
		LOG_ERR("XMODEMTRACE received unexpected response");
		return -EIO;
	}

	LOG_DBG("AT CMD: %s", log_strdup(AT_CMD_MAGPIO));
	err = nrf_modem_at_printf(AT_CMD_MAGPIO);
	if (err) {
		LOG_ERR("MAGPIO command failed");
		return -EIO;
	}

	LOG_DBG("AT CMD: %s", log_strdup(AT_CMD_COEX0));
	err = nrf_modem_at_printf(AT_CMD_COEX0);
	if (err) {
		LOG_ERR("MAGPIO command failed");
		return -EIO;
	}

	LOG_WRN("MAGPIO and COEX0 successfully configured");

#endif
	return 0;
}

void nrf_modem_lib_on_init_done(void)
{
	int err = thingy91_magpio_configure();

	if (err) {
		LOG_ERR("thingy91_magpio_configure failed with error: %d", err);
	}
}
