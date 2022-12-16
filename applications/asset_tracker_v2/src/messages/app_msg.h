/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _APP_MSG_H_
#define _APP_MSG_H_

/**
 * @brief Application message definitions
 * @defgroup app_msg Application messages
 * @{
 */

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defining application messages. */
#define APP_MSG_TYPES		\
	/* but why*/			\
	X(APP_MSG_START)		\
	X(APP_MSG_LTE_CONNECT)		\
	X(APP_MSG_LTE_DISCONNECT)	\
	X(APP_MSG_DATA_GET)		\
	X(APP_MSG_DATA_GET_ALL)		\
	X(APP_MSG_CONFIG_GET)		\
	X(APP_MSG_AGPS_NEEDED)		\
	X(APP_MSG_SHUTDOWN_READY)	\
	X(APP_MSG_ERROR)


/** @brief Data types that the application requests samples for in
 *	   @ref app_msg_type APP_MSG_DATA_GET.
 */
enum app_data_type {
	APP_DATA_ENVIRONMENTAL,
	APP_DATA_MOVEMENT,
	APP_DATA_MODEM_STATIC,
	APP_DATA_MODEM_DYNAMIC,
	APP_DATA_BATTERY,
	APP_DATA_LOCATION,
	APP_DATA_NEIGHBOR_CELLS,

	APP_DATA_COUNT,
};

/** @brief Application messages. */
struct app_msg {
	enum app_data_type data_list[APP_DATA_COUNT];

	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
	};

	size_t count;

	/** The time each module has to fetch data before what is available
	 *  is transmitted.
	 */
	int timeout;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _APP_MSG_H_ */
