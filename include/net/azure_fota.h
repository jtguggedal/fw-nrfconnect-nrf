/*
 *Copyright (c) 2020 Nordic Semiconductor ASA
 *
 *SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief Azure FOTA library header.
 */

#ifndef AZURE_FOTA_H__
#define AZURE_FOTA_H__

/**
 * @defgroup azure_fota Azure FOTA library
 * @{
 * @brief Library for performing FOTA with MQTT and HTTPS using Azure
 *	  IoT Hub and Azure Storage.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum azure_fota_evt_type {
	/** Azure FOTA has started */
	AZURE_FOTA_EVT_START,
	/** Azure FOTA complete and status reported to job document */
	AZURE_FOTA_EVT_DONE,
	/** Azure FOTA error */
	AZURE_FOTA_EVT_ERROR,
	/** Azure FOTA Erase pending*/
	AZURE_FOTA_EVT_ERASE_PENDING,
	/** Azure FOTA Erase done*/
	AZURE_FOTA_EVT_ERASE_DONE,
	/** Azure FOTA download progress */
	AZURE_FOTA_EVT_DL_PROGRESS,
};

#define AZURE_FOTA_EVT_DL_COMPLETE_VAL 100
struct azure_fota_event_dl {
	uint32_t progress; /* Download progress percent, 0-100 */
};

struct azure_fota_event {
	enum azure_fota_evt_type type;
	struct azure_fota_event_dl dl;
};

typedef void (*azure_fota_callback_t)(struct azure_fota_event *evt);

/**@brief Initialize the AWS Firmware Over the Air library.
 *
 * @param evt_handler  Callback function for events emitted by the azure_fota
 *                     library.
 *
 * @retval 0       If successfully initialized.
 * @retval -EINVAL If any of the input values are invalid.
 * @return         Negative value on error.
 */
int azure_fota_init(azure_fota_callback_t evt_handler);

int azure_fota_msg_process(const char *const buf, size_t len);

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* AZURE_FOTA_H__ */
