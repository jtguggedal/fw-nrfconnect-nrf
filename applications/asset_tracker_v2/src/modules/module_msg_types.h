/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MODULE_MSG_TYPES_H_
#define _MODULE_MSG_TYPES_H_

/**@file
 * @brief Modules common library header.
 */

/**
 * @defgroup modules_message_types Modules message type definitions
 * @{
 * @brief Defines the message types for all the modules in the application.
 */

#include <zephyr/kernel.h>

#include "app/app_module_msg.h"
#include "cloud/cloud_module_msg.h"
#include "data/data_module_msg.h"
#include "debug/debug_module_msg.h"
#include "led/led_state_msg.h"
#include "location/location_module_msg.h"
#include "modem/modem_module_msg.h"
#include "sensor/sensor_module_msg.h"
#include "ui/ui_module_msg.h"
#include "util/util_module_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

#undef X

#define MODULES_MESSAGE_TYPES		\
		APP_MODULE_MSG_TYPES	\
		CLOUD_MODULE_MSG_TYPES	\
		DATA_MODULE_MSG_TYPES	\
		DEBUG_MODULE_MSG_TYPES	\
		LOCATION_MODULE_MSG_TYPES	\
		MODEM_MODULE_MSG_TYPES	\
		SENSOR_MODULE_MSG_TYPES	\
		UI_MODULE_MSG_TYPES	\
		UTIL_MODULE_MSG_TYPES	\

enum module_msg_type {
#define X(_name)			\
	_name,
	MODULES_MESSAGE_TYPES
#undef X
	MODULES_MESSAGE_TYPE_COUNT
};

extern struct module_data *app_module;
extern struct module_data *cloud_module;
extern struct module_data *data_module;
extern struct module_data *debug_module;
extern struct module_data *location_module;
extern struct module_data *modem_module;
extern struct module_data *sensor_module;
extern struct module_data *ui_module;
extern struct module_data *util_module;

/**
 *@}
 */

#endif /* _MODULE_MSG_TYPES_H_ */
