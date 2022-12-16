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
#include "cloud_codec/cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_MSG_TYPES				\
	X(DATA_MSG_DATA_READY)			\
	X(DATA_MSG_DATA_SEND)			\
	X(DATA_MSG_DATA_SEND_BATCH)		\
	X(DATA_MSG_UI_DATA_SEND)		\
	X(DATA_MSG_UI_DATA_READY)		\
	X(DATA_MSG_IMPACT_DATA_READY)		\
	X(DATA_MSG_IMPACT_DATA_SEND)		\
	X(DATA_MSG_NEIGHBOR_CELLS_DATA_SEND)	\
	X(DATA_MSG_AGPS_REQUEST_DATA_SEND)	\
	X(DATA_MSG_CONFIG_INIT)			\
	X(DATA_MSG_CONFIG_READY)		\
	X(DATA_MSG_CONFIG_SEND)			\
	X(DATA_MSG_CONFIG_GET)			\
	X(DATA_MSG_DATE_TIME_OBTAINED)		\
	X(DATA_MSG_SHUTDOWN_READY)		\
	X(DATA_MSG_ERROR)

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
