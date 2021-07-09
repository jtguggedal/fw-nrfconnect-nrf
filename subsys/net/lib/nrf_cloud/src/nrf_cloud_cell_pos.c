/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <device.h>
#include <net/socket.h>
#include <nrf_socket.h>

#include <cJSON.h>
#include <cJSON_os.h>
#include <net/nrf_cloud_cell_pos.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(nrf_cloud_cell_pos, CONFIG_NRF_CLOUD_LOG_LEVEL);

#include "nrf_cloud_transport.h"
#include "nrf_cloud_codec.h"
#include "nrf_cloud_agps_schema_v1.h"

#define CELL_POS_JSON_CELL_LOC_KEY_DOREPLY	"doReply"

static bool json_initialized;

static cJSON *json_create_req_obj(const char *const app_id, const char *const msg_type)
{
	__ASSERT_NO_MSG(app_id != NULL);
	__ASSERT_NO_MSG(msg_type != NULL);

	if (!json_initialized) {
		cJSON_Init();
		json_initialized = true;
	}

	cJSON *resp_obj = cJSON_CreateObject();

	if (!cJSON_AddStringToObject(resp_obj, NRF_CLOUD_JSON_APPID_KEY, app_id) ||
	    !cJSON_AddStringToObject(resp_obj, NRF_CLOUD_JSON_MSG_TYPE_KEY, msg_type)) {
		cJSON_Delete(resp_obj);
		resp_obj = NULL;
	}

	return resp_obj;
}

static int json_send_to_cloud(cJSON *const cell_pos_request)
{
	__ASSERT_NO_MSG(cell_pos_request != NULL);

	char *msg_string;
	int err;

	msg_string = cJSON_PrintUnformatted(cell_pos_request);
	if (!msg_string) {
		LOG_ERR("Could not allocate memory for Cell Pos request message");
		return -ENOMEM;
	}

	LOG_DBG("Created Cell Pos request: %s", log_strdup(msg_string));

	struct nct_dc_data msg = {
		.data.ptr = msg_string,
		.data.len = strlen(msg_string)
	};

	err = nct_dc_send(&msg);
	if (err) {
		LOG_ERR("Failed to send Cell Pos request, error: %d", err);
	} else {
		LOG_DBG("Cell Pos request sent");
	}

	k_free(msg_string);

	return err;
}

static bool json_item_string_exists(const cJSON *const obj, const char *const key,
				    const char *const val)
{
	__ASSERT_NO_MSG(obj != NULL);
	__ASSERT_NO_MSG(key != NULL);

	char *str_val;
	cJSON *item = cJSON_GetObjectItem(obj, key);

	if (!item) {
		return false;
	}

	if (!val) {
		return cJSON_IsNull(item);
	}

	str_val = cJSON_GetStringValue(item);
	if (!str_val) {
		return false;
	}

	return (strcmp(str_val, val) == 0);
}

static int parse_cell_location_response(const char *const buf,
					struct nrf_cloud_cell_pos_result *result)
{
	int ret = 1; /* 1: cell-based location not found */
	cJSON *cell_pos_obj;
	cJSON *data_obj;

	if (buf == NULL) {
		return -EINVAL;
	}

	cell_pos_obj = cJSON_Parse(buf);
	if (!cell_pos_obj) {
		LOG_DBG("No JSON found for cell location");
		return 1;
	}

	/* First, check to see if this is a REST payload, which is not wrapped in
	 * an nRF Cloud MQTT message
	 */
	ret = nrf_cloud_parse_cell_pos_json(cell_pos_obj, result);
	if (ret == 0) {
		goto cleanup;
	}

	ret = 1;

	/* Check for nRF Cloud MQTT message; valid appId and msgType */
	if (!json_item_string_exists(cell_pos_obj, NRF_CLOUD_JSON_MSG_TYPE_KEY,
				     NRF_CLOUD_JSON_MSG_TYPE_VAL_DATA)) {
		LOG_DBG("Wrong msg type for cell location");
		goto cleanup;
	}

	if (json_item_string_exists(cell_pos_obj, NRF_CLOUD_JSON_APPID_KEY,
				    NRF_CLOUD_JSON_APPID_VAL_SINGLE_CELL)) {
		result->type = CELL_POS_TYPE_SINGLE;
	} else if (json_item_string_exists(cell_pos_obj, NRF_CLOUD_JSON_APPID_KEY,
					   NRF_CLOUD_JSON_APPID_VAL_MULTI_CELL)) {
		result->type = CELL_POS_TYPE_MULTI;
	} else {
		LOG_DBG("Wrong app id for cell location");
		goto cleanup;
	}

	data_obj = cJSON_GetObjectItem(cell_pos_obj, NRF_CLOUD_JSON_DATA_KEY);
	if (!data_obj) {
		LOG_DBG("Data object not found in cell-based location msg.");
		goto cleanup;
	}

	ret = nrf_cloud_parse_cell_pos_json(data_obj, result);

cleanup:
	cJSON_Delete(cell_pos_obj);
	return ret;
}

int nrf_cloud_cell_pos_request(enum nrf_cloud_cell_pos_type type, const bool request_loc)
{
	int err;
	cJSON *data_obj;
	cJSON *cell_pos_req_obj;

	/* TODO: currently single cell only */
	ARG_UNUSED(type);
	cell_pos_req_obj = json_create_req_obj(NRF_CLOUD_JSON_APPID_VAL_SINGLE_CELL,
					   NRF_CLOUD_JSON_MSG_TYPE_VAL_DATA);
	data_obj = cJSON_AddObjectToObject(cell_pos_req_obj, NRF_CLOUD_JSON_DATA_KEY);

	if (!cell_pos_req_obj || !data_obj) {
		err = -ENOMEM;
		goto cleanup;
	}

	/* Add modem info to the data object */
	err = nrf_cloud_json_add_modem_info(data_obj);
	if (err) {
		LOG_ERR("Failed to add modem info to cell loc req: %d", err);
		goto cleanup;
	}

	/* By default, nRF Cloud will send the location to the device */
	if (!request_loc) {
		/* Specify that location should not be sent to the device */
		cJSON_AddNumberToObject(data_obj, CELL_POS_JSON_CELL_LOC_KEY_DOREPLY, 0);
	}

	err = json_send_to_cloud(cell_pos_req_obj);

cleanup:
	cJSON_Delete(cell_pos_req_obj);

	return err;
}

int nrf_cloud_cell_pos_process(const char *buf, struct nrf_cloud_cell_pos_result *result)
{
	int err;

	if (!result) {
		return -EINVAL;
	}

	err = parse_cell_location_response(buf, result);
	if (err < 0) {
		LOG_ERR("Error processing cell-based location: %d", err);
	}

	return err;
}
