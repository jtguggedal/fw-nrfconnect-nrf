/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _DATA_MSG_H_
#define _DATA_MSG_H_

/**
 * @brief Data messages
 * @defgroup data_msg Data messages
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "cloud_codec/cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct data_msg;

#define DATA_MSG_CHAN		data_msg_chan
#define DATA_MSG_PAYLOAD_TYPE	struct data_msg
#define DATA_MSG_NO_PAYLOAD	struct data_msg	/* Zbus docs says union or struct required */

#define DATA_MSG_TYPES								\
	X(DATA_MSG_DATA_READY, 			DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_DATA_SEND, 			DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_DATA_SEND_BATCH, 		DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_UI_DATA_SEND, 		DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_UI_DATA_READY, 		DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_IMPACT_DATA_READY, 		DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_IMPACT_DATA_SEND, 		DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_NEIGHBOR_CELLS_DATA_SEND, 	DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_AGPS_REQUEST_DATA_SEND, 	DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_CONFIG_INIT, 		DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_CONFIG_READY, 		DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_CONFIG_SEND, 		DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_CONFIG_GET, 			DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_DATE_TIME_OBTAINED, 		DATA_MSG_NO_PAYLOAD)		\
	X(DATA_MSG_SHUTDOWN_READY, 		DATA_MSG_PAYLOAD_TYPE)		\
	X(DATA_MSG_ERROR, 			DATA_MSG_PAYLOAD_TYPE)

/** @brief Structure that contains a pointer to encoded data. */
struct data_buffer {
	char *buf;
	size_t len;
	/** Object paths used in lwM2M. NULL terminated. */
	char paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX]
		  [CONFIG_CLOUD_CODEC_LWM2M_PATH_ENTRY_SIZE_MAX];
	uint8_t valid_object_paths;
};

/** @brief Data messages. */
struct data_msg {
	union {
		/** Variable that carries a pointer to encoded data. */
		struct data_buffer buffer;
		/** Variable that carries the current device configuration. */
		struct cloud_data_cfg cfg;
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

#endif /* _DATA_MSG_H_ */
