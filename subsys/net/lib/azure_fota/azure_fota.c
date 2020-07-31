/*
 * Copyright 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdlib.h>
#include <string.h>
#include <net/fota_download.h>
#include <net/azure_iot_hub.h>
#include <net/azure_fota.h>
#include <cJSON.h>
#include <cJSON_os.h>

#include "azure_iot_hub_dps.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(azure_fota, CONFIG_AZURE_FOTA_LOG_LEVEL);

/* Enum to keep the fota status */
enum fota_status {
	STATUS_NONE = 0,
	STATUS_DOWNLOADING,
	STATUS_DOWNLOAD_DONE,
	STATUS_DOWNLOAD_ERROR,
	STATUS_APPLY_UPDATE,
	STATUS_REBOOT,
};

/* Static variables */
static azure_fota_callback_t callback;
static size_t download_progress;
static enum fota_status fota_state;
static char hostname[CONFIG_AZURE_FOTA_HOSTNAME_MAX_LEN];
static char file_path[CONFIG_AZURE_FOTA_FILE_PATH_MAX_LEN];

/* Static functions */

static void fota_dl_handler(const struct fota_download_evt *fota_evt)
{
	struct azure_fota_event evt;

	switch (fota_evt->id) {
	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("FOTA download completed evt received");

		fota_state = STATUS_DOWNLOAD_DONE;

		/* Always send download complete progress */
		evt.type = AZURE_FOTA_EVT_DL_PROGRESS;
		evt.dl.progress = AZURE_FOTA_EVT_DL_COMPLETE_VAL;
		callback(&evt);

		// TODO: Report status to device twin
		break;

	case FOTA_DOWNLOAD_EVT_ERASE_PENDING:
		evt.type = AZURE_FOTA_EVT_ERASE_PENDING;
		callback(&evt);
		break;

	case FOTA_DOWNLOAD_EVT_ERASE_DONE:
		evt.type = AZURE_FOTA_EVT_ERASE_DONE;
		callback(&evt);
		break;

	case FOTA_DOWNLOAD_EVT_ERROR:
		LOG_ERR("FOTA download failed, report back");
		fota_state = STATUS_NONE;
		// TODO: Report status to device twin
		evt.type = AZURE_FOTA_EVT_ERROR;
		callback(&evt);
		break;

	case FOTA_DOWNLOAD_EVT_PROGRESS:
		/* Only if CONFIG_FOTA_DOWNLOAD_PROGRESS_EVT is enabled */
		download_progress = fota_evt->progress;
		evt.type = AZURE_FOTA_EVT_DL_PROGRESS;
		evt.dl.progress = download_progress;
		callback(&evt);
		break;
	default:
		LOG_WRN("Unrecognized Azure FOTA event type: %d",
			fota_evt->id);
		break;
	}
}

/* @brief Extracts firmware image information from device twin JSON document.
 *
 * @retval 0 if successfully parsed, hostname and file_path populated.
 * @retval -ENOMSG Could not parse incoming buffer as JSON.
 * @retval -ENOMEM Could not find object, or insufficient memory to allocate it.
 */
static int extract_fw_details(const char *msg)
{
	int err = -ENOMEM;
	struct cJSON *root_obj, *desired_obj, *fw_obj, *fw_version_obj,
		*fw_location_obj, *fw_check_obj, *fw_hostname_obj, *fw_path_obj;
	uint32_t fw_version, fw_check;

	/* The properties.desired.firmware object has the excpected structure:
	 *	properties.desired.firmware : {
	 *		fwVersion : <version>,
	 *		fwLocation : {
	 *			host : <hostname>,
	 *			path : <file path>
	 *		},
	 *		fwCheckValue : <value>
	 *	}
	 */

	root_obj = cJSON_Parse(msg);
	if (root_obj == NULL) {
		printk("Could not parse message as JSON\n");
		return -ENOMSG;
	}

	/* Desired object */
	desired_obj = cJSON_GetObjectItemCaseSensitive(root_obj, "desired");
	if (desired_obj == NULL) {
		/* The received object can be the 'desired' object only, or
		 * the whole device twin properties object. If there is no
		 * 'desired' object to be found, assume it's the root object.
		 */
		LOG_DBG("No 'desired' object found, assuming it's root object");
		desired_obj = root_obj;
	}

	/* Firmware update information object */
	fw_obj = cJSON_GetObjectItemCaseSensitive(desired_obj, "firmware");
	if (fw_obj == NULL) {
		LOG_ERR("Failed to parse 'firmware' object");
		goto clean_exit;
	}

	/* Firmware version */
	fw_version_obj = cJSON_GetObjectItemCaseSensitive(fw_obj, "fwVersion");
	if (fw_version_obj == NULL) {
		printk("No fwVersion object found");
		goto clean_exit;
	}

	if (cJSON_IsString(fw_version_obj)) {
		fw_version = atoi(fw_version_obj->valuestring);
	} else if (cJSON_IsNumber(fw_version_obj)) {
		fw_version = fw_version_obj->valueint;
	} else {
		printk("Invalid fwVersion format received\n");
		goto clean_exit;
	}

	LOG_DBG("Firmware update received for version %d", fw_version);

	/* Firmware image location */
	fw_location_obj = cJSON_GetObjectItemCaseSensitive(fw_obj,
							   "fwLocation");
	if (fw_location_obj == NULL) {
		printk("No fwLocation object found");
		goto clean_exit;
	}

	/* Firmware image hostname */
	fw_hostname_obj = cJSON_GetObjectItemCaseSensitive(fw_location_obj,
							   "host");
	if (fw_hostname_obj == NULL) {
		printk("No hostname object found");
		goto clean_exit;
	}

	if (strlen(fw_hostname_obj->valuestring) >= sizeof(hostname)) {
		LOG_ERR("Received hostname larger (%d) than buffer (%d)",
			strlen(fw_hostname_obj->valuestring), sizeof(hostname));
		goto clean_exit;
	}

	memcpy(hostname, fw_hostname_obj->valuestring, sizeof(hostname));

	/* Firmware image file path */
	fw_path_obj = cJSON_GetObjectItemCaseSensitive(fw_location_obj, "path");
	if (fw_path_obj == NULL) {
		printk("No path object found");
		goto clean_exit;
	}

	if (strlen(fw_path_obj->valuestring) >= sizeof(file_path)) {
		LOG_ERR("Received file path larger (%d) than buffer (%d)",
			strlen(fw_path_obj->valuestring), sizeof(file_path));
		goto clean_exit;
	}

	memcpy(file_path, fw_path_obj->valuestring, sizeof(file_path));

	fw_check_obj = cJSON_GetObjectItemCaseSensitive(fw_obj, "fwCheckValue");
	if (fw_check_obj == NULL) {
		printk("Failed to parse 'fwCheckValue' object");
		goto clean_exit;
	}

	// TODO: Do something sensible with this check value
	fw_check = fw_check_obj->valueint;

	LOG_DBG("Firmware check value: %d", fw_check);

	err = 0;

clean_exit:
	cJSON_Delete(root_obj);

	return err;
}

/* Public functions */
int azure_fota_init(azure_fota_callback_t evt_handler)
{
	int err;

	if (evt_handler == NULL) {
		return -EINVAL;
	}

	callback = evt_handler;

	err = fota_download_init(fota_dl_handler);
	if (err != 0) {
		LOG_ERR("fota_download_init error %d", err);
		return err;
	}

	cJSON_Init();

	return 0;
}

int azure_fota_msg_process(const char *const buf, size_t len)
{
	int err;
	struct azure_fota_event evt = {
		.type = AZURE_FOTA_EVT_START
	};

	err = extract_fw_details(buf);
	if (err == -ENOMSG) {
		LOG_DBG("No 'firmware' object found, ignoring message");
		return 0;
	} else if (err < 0) {
		LOG_ERR("Failed to process FOTA image details, error: %d", err);
		return err;
	}

	if (fota_state == STATUS_DOWNLOADING) {
		LOG_INF("Firmware download is already ongoing");
		return 0;
	}

	// TODO: Remove hack to report initial download status
	struct fota_download_evt fota_evt = {
		.id = FOTA_DOWNLOAD_EVT_PROGRESS,
		.progress = 0,
	};
	fota_dl_handler(&fota_evt);

	callback(&evt);

	LOG_INF("Start downloading firmware from %s/%s",
		log_strdup(hostname), log_strdup(file_path));

	err = fota_download_start(hostname, file_path,
				  CONFIG_AZURE_FOTA_DOWNLOAD_SECURITY_TAG,
				  CONFIG_AZURE_FOTA_DOWNLOAD_PORT, NULL);
	if (err) {
		LOG_ERR("Error (%d) when trying to start firmware download",
			err);
		return err;
	}

	fota_state = STATUS_DOWNLOADING;

	return 0;
}

