/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _DEBUG_MSG_H_
#define _DEBUG_MSG_H_

/**
 * @brief Debug messages
 * @defgroup debug_msg Debug messages
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct debug_msg;

#define DEBUG_MSG_CHAN		debug_msg_chan
#define DEBUG_MSG_PAYLOAD_TYPE	struct debug_msg
#define DEBUG_MSG_NO_PAYLOAD	struct debug_msg	/* Zbus docs says union or struct required */

#define DEBUG_MSG_TYPES										\
	X(DEBUG_MSG_MEMFAULT_DATA_READY, 		DEBUG_MSG_PAYLOAD_TYPE)			\
	X(DEBUG_MSG_QEMU_X86_INITIALIZED, 		DEBUG_MSG_NO_PAYLOAD)			\
	X(DEBUG_MSG_QEMU_X86_NETWORK_CONNECTED, 	DEBUG_MSG_NO_PAYLOAD)			\
	X(DEBUG_MSG_ERROR, 				DEBUG_MSG_PAYLOAD_TYPE)

struct debug_memfault_data {
	uint8_t *buf;
	size_t len;
};

/** @brief Debug message. */
struct debug_msg {
	union {
		struct debug_memfault_data memfault;
		/** Code signifying the cause of error. */
		int err;
	};
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _DEBUG_MSG_H_ */
