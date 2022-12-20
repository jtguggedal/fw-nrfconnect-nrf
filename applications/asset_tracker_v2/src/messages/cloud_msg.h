/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CLOUD_MSG_H_
#define _CLOUD_MSG_H_

/**
 * @brief Cloud messages
 * @defgroup cloud_msg Cloud messages
 * @{
 */

#include <zephyr/kernel.h>
#include <qos.h>
#include <zephyr/zbus/zbus.h>

// TODO: Check if needed, dependency to other module must be avoided
#include "data/cloud_codec/cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct cloud_msg;

#define CLOUD_MSG_CHAN		cloud_msg_chan
#define CLOUD_MSG_PAYLOAD_TYPE	struct cloud_msg
#define CLOUD_MSG_NO_PAYLOAD	struct cloud_msg	/* Zbus docs says union or struct required */

#define CLOUD_MSG_TYPES									\
	X(CLOUD_MSG_CONNECTED, 			CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_DISCONNECTED, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_CONNECTING, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_CONNECTION_TIMEOUT, 	CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_LTE_CONNECT, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_LTE_DISCONNECT, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_USER_ASSOCIATION_REQUEST, 	CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_USER_ASSOCIATED, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_REBOOT_REQUEST, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_CONFIG_RECEIVED, 		CLOUD_MSG_PAYLOAD_TYPE)			\
	X(CLOUD_MSG_CONFIG_EMPTY, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_FOTA_START, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_FOTA_DONE, 			CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_FOTA_ERROR, 		CLOUD_MSG_NO_PAYLOAD)			\
	X(CLOUD_MSG_DATA_SEND_QOS, 		CLOUD_MSG_PAYLOAD_TYPE)			\
	X(CLOUD_MSG_SHUTDOWN_READY, 		CLOUD_MSG_PAYLOAD_TYPE)			\
	X(CLOUD_MSG_ERROR, 			CLOUD_MSG_PAYLOAD_TYPE)

/** @brief Structure used to acknowledge messages. */
struct cloud_data_ack {
	/** Pointer to data that was attempted to be sent. */
	void *ptr;
	/** Length of data that was attempted to be sent. */
	size_t len;
};

/** @brief Cloud messages. */
struct cloud_msg {
	union {
		/** Variable that contains a new configuration received from the cloud service. */
		struct cloud_data_cfg config;
		/** Variable that contains data that was attempted to be sent. Could be used
		 *  to free allocated data post transmission.
		 */
		struct cloud_data_ack ack;
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

#endif /* _CLOUD_MSG_H_ */
