/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _DEBUG_MODULE_MSG_H_
#define _DEBUG_MODULE_MSG_H_

/**
 * @brief Debug module messages
 * @defgroup debug_module_msg Debug module messages
 * @{
 */

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_MODULE_MSG_TYPES				\
	X(DEBUG_MSG_MEMFAULT_DATA_READY)		\
	X(DEBUG_MSG_QEMU_X86_INITIALIZED)		\
	X(DEBUG_MSG_QEMU_X86_NETWORK_CONNECTED)		\
	X(DEBUG_MSG_ERROR)

struct debug_module_memfault_data {
	uint8_t *buf;
	size_t len;
};

/** @brief Debug message. */
struct debug_module_msg {
	union {
		struct debug_module_memfault_data memfault;
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

#endif /* _DEBUG_MODULE_MSG_H_ */
