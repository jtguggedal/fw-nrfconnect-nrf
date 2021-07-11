/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NRF_CLOUD_CODEC_H__
#define NRF_CLOUD_CODEC_H__

#include <stdbool.h>
#include <modem/modem_info.h>
#include <modem/lte_lc.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_pgps.h>
#include <net/nrf_cloud_agps.h>
#include <net/nrf_cloud_cell_pos.h>
#include "cJSON.h"
#include "nrf_cloud_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NRF_CLOUD_JSON_APPID_KEY		"appId"
#define NRF_CLOUD_JSON_APPID_VAL_AGPS		"AGPS"
#define NRF_CLOUD_JSON_APPID_VAL_PGPS		"PGPS"
/* TODO: SCELL and MCELL can be removed, new API is simply "LOCATE" */
#define NRF_CLOUD_JSON_APPID_VAL_SINGLE_CELL	"SCELL"
#define NRF_CLOUD_JSON_APPID_VAL_LOCATE		"LOCATE"
#define NRF_CLOUD_JSON_APPID_VAL_MULTI_CELL	NRF_CLOUD_JSON_APPID_VAL_LOCATE

#define NRF_CLOUD_JSON_MSG_TYPE_KEY		"messageType"
#define NRF_CLOUD_JSON_MSG_TYPE_VAL_DATA	"DATA"

#define NRF_CLOUD_JSON_DATA_KEY			"data"

/* Modem info key text */
#define NRF_CLOUD_JSON_MCC_KEY			"mcc"
#define NRF_CLOUD_JSON_MNC_KEY			"mnc"
#define NRF_CLOUD_JSON_AREA_CODE_KEY		"tac"
#define NRF_CLOUD_JSON_CELL_ID_KEY		"eci"
#define NRF_CLOUD_JSON_PHYCID_KEY		"phycid"

/* Cellular positioning */
#define NRF_CLOUD_CELL_POS_JSON_KEY_LOC		"location"
#define NRF_CLOUD_CELL_POS_JSON_KEY_LNG		"lng"
#define NRF_CLOUD_CELL_POS_JSON_KEY_LAT		"lat"
#define NRF_CLOUD_CELL_POS_JSON_KEY_ACC		"accuracy"
#define NRF_CLOUD_CELL_POS_JSON_KEY_LON		"lon"
#define NRF_CLOUD_CELL_POS_JSON_KEY_UNCERT	"uncertainty"

#define NRF_CLOUD_CELL_POS_JSON_KEY_LTE		"lte"
#define NRF_CLOUD_CELL_POS_JSON_KEY_ECI		"cid"
#define NRF_CLOUD_CELL_POS_JSON_KEY_MCC		NRF_CLOUD_JSON_MCC_KEY
#define NRF_CLOUD_CELL_POS_JSON_KEY_MNC		NRF_CLOUD_JSON_MNC_KEY
#define NRF_CLOUD_CELL_POS_JSON_KEY_TAC		NRF_CLOUD_JSON_AREA_CODE_KEY
#define NRF_CLOUD_CELL_POS_JSON_KEY_AGE		"age"
#define NRF_CLOUD_CELL_POS_JSON_KEY_T_ADV	"adv"

#define NRF_CLOUD_CELL_POS_JSON_KEY_EARFCN	"earfcn"
#define NRF_CLOUD_CELL_POS_JSON_KEY_PCI		"pci"
#define NRF_CLOUD_CELL_POS_JSON_KEY_NBORS	"nmr"
#define NRF_CLOUD_CELL_POS_JSON_KEY_RSRP	"rsrp"
#define NRF_CLOUD_CELL_POS_JSON_KEY_RSRQ	"rsrq"

/* P-GPS */
#define NRF_CLOUD_JSON_PGPS_PRED_COUNT		"predictionCount"
#define NRF_CLOUD_JSON_PGPS_INT_MIN		"predictionIntervalMinutes"
#define NRF_CLOUD_JSON_PGPS_GPS_DAY		"startGpsDay"
#define NRF_CLOUD_JSON_PGPS_GPS_TIME		"startGpsTimeOfDaySeconds"
#define NRF_CLOUD_PGPS_RCV_ARRAY_IDX_HOST 0
#define NRF_CLOUD_PGPS_RCV_ARRAY_IDX_PATH 1

/**@brief Initialize the codec used encoding the data to the cloud. */
int nrf_cloud_codec_init(void);

/**@brief Encode the sensor data based on the indicated type. */
int nrf_cloud_encode_sensor_data(const struct nrf_cloud_sensor_data *input,
				 struct nrf_cloud_data *output);

/**@brief Encode the sensor data to be sent to the device shadow. */
int nrf_cloud_encode_shadow_data(const struct nrf_cloud_sensor_data *sensor,
				 struct nrf_cloud_data *output);

/**@brief Encode the user association data based on the indicated type. */
int nrf_cloud_decode_requested_state(const struct nrf_cloud_data *payload,
				     enum nfsm_state *requested_state);

/**@brief Decodes data endpoint information. */
int nrf_cloud_decode_data_endpoint(const struct nrf_cloud_data *input,
				   struct nrf_cloud_data *tx_endpoint,
				   struct nrf_cloud_data *rx_endpoint,
				   struct nrf_cloud_data *m_endpoint);

/** @brief Encodes state information. */
int nrf_cloud_encode_state(uint32_t reported_state, struct nrf_cloud_data *output);

/** @brief Search input for config and encode response if necessary. */
int nrf_cloud_encode_config_response(struct nrf_cloud_data const *const input,
				     struct nrf_cloud_data *const output,
				     bool *const has_config);

void nrf_cloud_fota_job_free(struct nrf_cloud_fota_job_info *const job);

int nrf_cloud_rest_fota_execution_parse(const char *const response,
					struct nrf_cloud_fota_job_info *const job);

int nrf_cloud_parse_pgps_response(const char *const response,
				  struct nrf_cloud_pgps_result *const result);

int nrf_cloud_json_add_modem_info(cJSON * const data_obj);

char * const nrf_cloud_format_cell_pos_req_payload(struct lte_lc_cells_info const *const inf,
						   size_t inf_cnt);

int nrf_cloud_parse_cell_pos_response(const char *const buf,
				      struct nrf_cloud_cell_pos_result *result);

int get_string_from_array(const cJSON * const array, const int index,
			  char **string_out);

int get_string_from_obj(const cJSON * const obj, const char *const key,
			char **string_out);

#ifdef __cplusplus
}
#endif

#endif /* NRF_CLOUD_CODEC_H__ */
