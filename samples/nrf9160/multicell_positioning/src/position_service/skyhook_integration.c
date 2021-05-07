/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>

#include "position_service.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(multicell_positioning_skyhook, CONFIG_MULTICELL_POSITIONING_LOG_LEVEL);

#define API_KEY 	CONFIG_MULTICELL_SKYHOOK_API_KEY
#define HOSTNAME	CONFIG_MULTICELL_SKYHOOK_HOSTNAME


// https://global.skyhookwireless.com/wps2/json/location?key=<YOUR API KEY>&user=<DEVICE SERIAL NUM, MAC ADDRESS, OR OTHER UNIQUE ID>

#define HTTP_REQUEST_HEADER						\
	"POST /wps2/json/location?key="API_KEY"&user=%s HTTP/1.1\r\n" \
	"Host: "HOSTNAME"\r\n"					        \
	"Content-Type: application/json\r\n"				\
	"Content-Length: %d\r\n\r\n"

#define HTTP_REQUEST_BODY                                               \
	"{"								\
		"\"considerIp\": \"false\","				\
		"\"cellTowers\": ["					\
			"{"						\
				"\"radioType\": \"%s\","			\
				"\"mobileCountryCode\": %d,"		\
				"\"mobileNetworkCode\": %d,"		\
				"\"locationAreaCode\": %d,"		\
				"\"cellId\": %d,"			\
				"\"neighborId\": %d,"			\
				"\"timingAdvance\": %d,"		\
				"\"signalStrength\": %d,"		\
				"\"channel\": %d,"			\
				"\"serving\": true"			\
			"},"						\
			"%s"						\
		"]"							\
	"}"

#define HTTP_REQUEST_BODY_NO_NEIGHBORS					\
	"{"								\
		"\"considerIp\": \"false\","				\
		"\"cellTowers\": ["					\
			"{"						\
				"\"radioType\": \"%s\","			\
				"\"mobileCountryCode\": %d,"		\
				"\"mobileNetworkCode\": %d,"		\
				"\"locationAreaCode\": %d,"		\
				"\"cellId\": %d,"			\
				"\"neighborId\": %d,"			\
				"\"timingAdvance\": %d,"		\
				"\"signalStrength\": %d,"		\
				"\"channel\": %d,"			\
				"\"serving\": true"			\
			"}"						\
		"]"							\
	"}"

#define HTTP_REQUEST_BODY_NEIGHBOR_ELEMENT				\
	"{"								\
		"\"radioType\": \"%s\","				\
		"\"neighborId\": %d,"					\
		"\"signalStrength\": %d,"				\
		"\"channel\": %d,"					\
		"\"serving\": false"					\
	"}"

static const char tls_certificate[] =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
	"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
	"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
	"QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n"
	"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
	"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n"
	"9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n"
	"CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n"
	"nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n"
	"43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n"
	"T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n"
	"gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n"
	"BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n"
	"TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n"
	"DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n"
	"hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n"
	"06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n"
	"PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n"
	"YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n"
	"CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n"
	"-----END CERTIFICATE-----\n";

BUILD_ASSERT(sizeof(API_KEY) > 1, "API key must be configured");
BUILD_ASSERT(sizeof(HOSTNAME) > 1, "Hostname must be configured");

const char *position_service_get_hostname(void)
{
	return HOSTNAME;
}

const char *position_service_get_certificate(void)
{
	return tls_certificate;
}

static int get_rsrp(int input)
{
	if (input <= 0) {
		return input - 140;
	}

	return input - 141;
}

int position_service_generete_request(struct lte_lc_cells_info *cell_data,
				      char *buf, size_t buf_len)
{
	if (cell_data->current_cell.id == 0) {
		LOG_WRN("No cells were found");
		return -ENOENT;
	}

	char body[512];
	char neighbors[512];
	enum lte_lc_lte_mode mode;
	int err;
	char imei[20];

	err = modem_info_init();
	if (err) {
		LOG_ERR("modem_info_init failed, error: %d", err);
		return err;
	}

	err = modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
	if (err < 0) {
		LOG_ERR("Failed to get IMEI, error: %d", err);
		LOG_WRN("Falling back to uptime as user ID");
		snprintk(imei, sizeof(imei), "%d", k_cycle_get_32());
	} else {
		/* Null-terminate the IMEI. */
		imei[15] = '\0';
	}

	err = lte_lc_lte_mode_get(&mode);
	if (err) {
		LOG_ERR("Failed to get current LTE mode (error %d), fallback to LTE-M", err);
		mode = LTE_LC_LTE_MODE_LTEM;
	}

	if (cell_data->ncells_count == 0) {
		snprintk(body, sizeof(body), HTTP_REQUEST_BODY_NO_NEIGHBORS,
			 mode == LTE_LC_LTE_MODE_LTEM ? "lte" : "nbiot",
			 cell_data->current_cell.mcc,
			 cell_data->current_cell.mnc,
			 cell_data->current_cell.tac,
			 cell_data->current_cell.id,
			 cell_data->current_cell.phys_cell_id,
			 cell_data->current_cell.timing_advance,
			 get_rsrp(cell_data->current_cell.rsrp),
			 cell_data->current_cell.earfcn);

		snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", imei, strlen(body), body);

		return 0;
	}

	*neighbors = 0;

	for (size_t i = 0; i < cell_data->ncells_count; i++) {
		char element[100];
		snprintk(element, sizeof(element), HTTP_REQUEST_BODY_NEIGHBOR_ELEMENT "%s",
			 mode == LTE_LC_LTE_MODE_LTEM ? "lte" : "nbiot",
			 cell_data->neighbor_cells[i].phys_cell_id,
			 get_rsrp(cell_data->current_cell.rsrp),
			 cell_data->neighbor_cells[i].earfcn,
			 i + 1 < cell_data->ncells_count ? "," : "");
		strcat(neighbors, element);
	}

	snprintk(body, sizeof(body), HTTP_REQUEST_BODY,
		 mode == LTE_LC_LTE_MODE_LTEM ? "lte" : "nbiot",
		 cell_data->current_cell.mcc,
		 cell_data->current_cell.mnc,
		 cell_data->current_cell.tac,
		 cell_data->current_cell.id,
		 cell_data->current_cell.phys_cell_id,
		 cell_data->current_cell.timing_advance,
		 get_rsrp(cell_data->current_cell.rsrp),
		 cell_data->current_cell.earfcn,
		 neighbors);

	snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", imei, strlen(body), body);

	return 0;
}
