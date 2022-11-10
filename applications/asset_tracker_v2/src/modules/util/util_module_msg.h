/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UTIL_MODULE_MSG_H_
#define _UTIL_MODULE_MSG_H_

/**
 * @brief Util module messages
 * @defgroup util_module_message Utility module messages
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#define UTIL_MODULE_MSG_TYPES	\
	X(UTIL_MSG_SHUTDOWN_REQUEST)

/** @brief Shutdown reason included in shutdown requests from the utility module. */
enum shutdown_reason {
	/** Generic reason, typically an irrecoverable error. */
	REASON_GENERIC,
	/** The application shuts down because a FOTA update finished. */
	REASON_FOTA_UPDATE,
};

/** @brief Utility module messages. */
struct util_module_msg {
	/** Variable that contains the reason for the shutdown request. */
	enum shutdown_reason reason;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _UTIL_MODULE_MSG_H_ */
