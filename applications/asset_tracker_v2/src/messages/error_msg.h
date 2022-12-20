/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _ERROR_MSG_H_
#define _ERROR_MSG_H_

/**
 * @brief Error messages
 * @defgroup error_message Error messages
 * @{
 */

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct error_msg;

#define ERROR_MSG_CHAN			error_msg_chan
#define ERROR_MSG_PAYLOAD_TYPE		struct error_msg

#define ERROR_MSG_TYPES	\
	X(ERROR_MSG, ERROR_MSG_PAYLOAD_TYPE)

/** @brief Error messages. */
struct error_msg {
	/** Error code. */
	int error_code;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _ERROR_MSG_H_ */
