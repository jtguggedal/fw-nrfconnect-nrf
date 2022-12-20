/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CHANNELS_H_
#define _CHANNELS_H_

#include <zephyr/zbus/zbus.h>

/* Declare all channels so that they're available to all modules */
ZBUS_CHAN_DECLARE(APP_MSG_CHAN,
		  CLOUD_MSG_CHAN,
		  DATA_MSG_CHAN,
		  DEBUG_MSG_CHAN,
		  LOCATION_MSG_CHAN,
		  MODEM_MSG_CHAN,
		  SENSOR_MSG_CHAN,
		  UI_MSG_CHAN,
		  UTIL_MSG_CHAN);

#ifdef __cplusplus
}
#endif

#endif /* _CHANNELS_H_ */
