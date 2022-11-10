/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UI_MODULE_MSG_H_
#define _UI_MODULE_MSG_H_

/**
 * @brief UI module messages
 * @defgroup ui_module_msg UI module messages
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MODULE_MSG_TYPES		\
	X(UI_MSG_BUTTON_DATA_READY)	\
	X(UI_MSG_SHUTDOWN_READY)	\
	X(UI_MSG_ERROR)

/** @brief Structure used to provide button data. */
struct ui_button_data {
	/** Button number of the board that was pressed. */
	int button_number;
	/** Uptime when the button was pressed. */
	int64_t timestamp;
};

/** @brief UI module messages. */
struct ui_module_msg {
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

#endif /* _UI_MODULE_MSG_H_ */
