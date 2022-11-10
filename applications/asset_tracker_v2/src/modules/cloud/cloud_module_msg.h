/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CLOUD_MODULE_MSG_H_
#define _CLOUD_MODULE_MSG_H_

/**
 * @brief Cloud module messages
 * @defgroup cloud_module_msg Cloud module messages
 * @{
 */

#include <zephyr/kernel.h>
#include <qos.h>

// TODO: Check if needed, dependency to other module must be avoided
#include "data/cloud_codec/cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_MODULE_MSG_TYPES			\
	X(CLOUD_MSG_CONNECTED)			\
	X(CLOUD_MSG_DISCONNECTED)		\
	X(CLOUD_MSG_CONNECTING)			\
	X(CLOUD_MSG_CONNECTION_TIMEOUT)		\
	X(CLOUD_MSG_LTE_CONNECT)		\
	X(CLOUD_MSG_LTE_DISCONNECT)		\
	X(CLOUD_MSG_USER_ASSOCIATION_REQUEST)	\
	X(CLOUD_MSG_USER_ASSOCIATED)		\
	X(CLOUD_MSG_REBOOT_REQUEST)		\
	X(CLOUD_MSG_CONFIG_RECEIVED)		\
	X(CLOUD_MSG_CONFIG_EMPTY)		\
	X(CLOUD_MSG_FOTA_START)			\
	X(CLOUD_MSG_FOTA_DONE)			\
	X(CLOUD_MSG_FOTA_ERROR)			\
	X(CLOUD_MSG_DATA_SEND_QOS)		\
	X(CLOUD_MSG_SHUTDOWN_READY)		\
	X(CLOUD_MSG_ERROR)

/** @brief Structure used to acknowledge messages sent to the cloud module. */
struct cloud_module_data_ack {
	/** Pointer to data that was attempted to be sent. */
	void *ptr;
	/** Length of data that was attempted to be sent. */
	size_t len;
};

/** @brief Cloud module messages. */
struct cloud_module_msg {
	union {
		/** Variable that contains a new configuration received from the cloud service. */
		struct cloud_data_cfg config;
		/** Variable that contains data that was attempted to be sent. Could be used
		 *  to free allocated data post transmission.
		 */
		struct cloud_module_data_ack ack;
		/** Variable that contains the message that should be sent to cloud. */
		struct qos_data message;
		/** Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
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

#endif /* _CLOUD_MODULE_MSG_H_ */
