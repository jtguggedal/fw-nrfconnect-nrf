/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UI_MSG_H_
#define _UI_MSG_H_

/**
 * @brief UI messages
 * @defgroup ui_msg UI messages
 * @{
 */

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct ui_msg;

#define UI_MSG_CHAN			ui_msg_chan
#define UI_MSG_PAYLOAD_TYPE		struct ui_msg

#define UI_MSG_TYPES						\
	X(UI_MSG_BUTTON_DATA_READY, 	UI_MSG_PAYLOAD_TYPE)	\
	X(UI_MSG_SHUTDOWN_READY, 	UI_MSG_PAYLOAD_TYPE)	\
	X(UI_MSG_ERROR, 		UI_MSG_PAYLOAD_TYPE)

/** @brief Structure used to provide button data. */
struct ui_button_data {
	/** Button number of the board that was pressed. */
	int button_number;
	/** Uptime when the button was pressed. */
	int64_t timestamp;
};

/** @brief UI messages. */
struct ui_msg {
	union {
		/** Variable that carries button press information. */
		struct ui_button_data btn;
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

#endif /* _UI_MSG_H_ */
