/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/zbus/zbus.h>

#include "msg_channels.h"
#include "msg_definitions.h"

#define MSG_CHANNELS							\
	X(APP_MSG_CHAN,		APP_MSG_PAYLOAD_TYPE)			\
	X(CLOUD_MSG_CHAN,	CLOUD_MSG_PAYLOAD_TYPE)			\
	X(DATA_MSG_CHAN,	DATA_MSG_PAYLOAD_TYPE)			\
	X(DEBUG_MSG_CHAN,	DEBUG_MSG_PAYLOAD_TYPE)			\
	X(ERROR_MSG_CHAN,	ERROR_MSG_PAYLOAD_TYPE)			\
	X(LOCATION_MSG_CHAN,	LOCATION_MSG_PAYLOAD_TYPE)		\
	X(MODEM_MSG_CHAN,	MODEM_MSG_PAYLOAD_TYPE)			\
	X(SENSOR_MSG_CHAN,	SENSOR_MSG_PAYLOAD_TYPE)		\
	X(UI_MSG_CHAN,		UI_MSG_PAYLOAD_TYPE)			\
	X(UTIL_MSG_CHAN,	UTIL_MSG_PAYLOAD_TYPE)

#define X(_name, _data_type)		\
	ZBUS_CHAN_DEFINE(_name, _data_type, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
	MSG_CHANNELS
#undef X

// /* Define all channels in the application */
// ZBUS_CHAN_DEFINE(APP_MSG_CHAN,
// 		 APP_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(CLOUD_MSG_CHAN,
// 		 CLOUD_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(DATA_MSG_CHAN,
// 		 DATA_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(DEBUG_MSG_CHAN,
// 		 DEBUG_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(ERROR_MSG_CHAN,
// 		 ERROR_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(LOCATION_MSG_CHAN,
// 		 LOCATION_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(MODEM_MSG_CHAN,
// 		 MODEM_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(SENSOR_MSG_CHAN,
// 		 SENSOR_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(UI_MSG_CHAN,
// 		 UI_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
// ZBUS_CHAN_DEFINE(UTIL_MSG_CHAN,
// 		 UTIL_MSG_PAYLOAD_TYPE,
// 		 NULL, NULL,
// 		 ZBUS_OBSERVERS_EMPTY,
// 		 ZBUS_MSG_INIT(0));
