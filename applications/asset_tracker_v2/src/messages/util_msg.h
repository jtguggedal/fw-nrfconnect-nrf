/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UTIL_MSG_H_
#define _UTIL_MSG_H_

/**
 * @brief Util messages
 * @defgroup util_message Utility messages
 * @{
 */

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct util_msg;

#define UTIL_MSG_CHAN			util_msg_chan
#define UTIL_MSG_PAYLOAD_TYPE		struct util_msg

#define UTIL_MSG_TYPES	\
	X(UTIL_MSG_SHUTDOWN_REQUEST, SENSOR_MSG_PAYLOAD_TYPE)

/** @brief Shutdown reason included in shutdown requests. */
enum shutdown_reason {
	/** Generic reason, typically an irrecoverable error. */
	REASON_GENERIC,
	/** The application shuts down because a FOTA update finished. */
	REASON_FOTA_UPDATE,
};

/** @brief Utility messages. */
struct util_msg {
	/** Variable that contains the reason for the shutdown request. */
	enum shutdown_reason reason;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _UTIL_MSG_H_ */
