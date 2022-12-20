/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MSG_TYPES_H_
#define _MSG_TYPES_H_

/**@file
 * @brief Modules common library header.
 */

/**
 * @defgroup message_types Modules message type definitions
 * @{
 * @brief Defines the message types for all the modules in the application.
 */

#include <zephyr/kernel.h>

#include "app_msg.h"
#include "cloud_msg.h"
#include "data_msg.h"
#include "debug_msg.h"
#include "led_state_msg.h"
#include "location_msg.h"
#include "modem_msg.h"
#include "sensor_msg.h"
#include "ui_msg.h"
#include "util_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MESSAGE_TYPES			\
		APP_MSG_TYPES		\
		CLOUD_MSG_TYPES		\
		DATA_MSG_TYPES		\
		DEBUG_MSG_TYPES		\
		LOCATION_MSG_TYPES	\
		MODEM_MSG_TYPES		\
		SENSOR_MSG_TYPES	\
		UI_MSG_TYPES		\
		UTIL_MSG_TYPES

enum module_msg_type {
#define X(_name, _data_type)		\
	_name,
	MESSAGE_TYPES
#undef X
	/* Modules can implement private message types with enum value starting from
	  * MESSAGE_TYPE_PUBLIC_COUNT.
	 */
	MESSAGE_TYPE_PUBLIC_COUNT
};

/**
 *@}
 */

#endif /* _MSG_TYPES_H_ */
