/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CHANNELS_H_
#define _CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#define CHANNEL_LISTENER_TO_QUEUE(_chan, _name)							  \
	static void MODULE##_##_name##_cb(const struct zbus_channel *_chan)			  \
	{											  \
		struct module_msg *_chan##_msg = (struct module_msg *)zbus_chan_const_msg(_chan); \
												  \
		module_enqueue_msg(&self, _chan##_msg);						  \
	}											  \
												  \
	static ZBUS_LISTENER_DEFINE(_name, MODULE##_##_name##_cb);

#define CHANNEL_LISTENER_TO_HANDLER(_chan, _name)							  \
	static void MODULE##_##_name##_cb(const struct zbus_channel *_chan)			  \
	{											  \
		struct module_msg *_chan##_msg = (struct module_msg *)zbus_chan_const_msg(_chan); \
												  \
		message_handler(_chan##_msg);						  \
	}											  \
												  \
	static ZBUS_LISTENER_DEFINE(_name, MODULE##_##_name##_cb);

/* Declare all channels so that they're available to all modules */
ZBUS_CHAN_DECLARE(APP_MSG_CHAN,
		  CLOUD_MSG_CHAN,
		  DATA_MSG_CHAN,
		  DEBUG_MSG_CHAN,
		  ERROR_MSG_CHAN,
		  LOCATION_MSG_CHAN,
		  MODEM_MSG_CHAN,
		  SENSOR_MSG_CHAN,
		  UI_MSG_CHAN,
		  UTIL_MSG_CHAN);

#ifdef __cplusplus
}
#endif

#endif /* _CHANNELS_H_ */
