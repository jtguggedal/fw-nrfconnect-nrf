/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <cJSON_os.h>

#include "location_service.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(multicell_location_polte, CONFIG_MULTICELL_LOCATION_LOG_LEVEL);

#define API_KEY 	CONFIG_MULTICELL_LOCATION_POLTE_API_TOKEN
#define HOSTNAME	CONFIG_MULTICELL_LOCATION_HOSTNAME

/* The timing advance returned by the nRF9160 modem must be divided by 16
 * to have the range expected by Skyhook.
 */
#define TA_DIVIDER	16

/* Polte expects the following URL format:
 *	https://polte.io/customer/<customerId>/locate-core
 *
 * The HTTP POST r
 */

#define HTTP_REQUEST_HEADER						\
	"POST /api/v1/customer/"					\
		CONFIG_MULTICELL_LOCATION_POLTE_CUSTOMER_ID		\
		"/locate-core?excludeLocationMetrics=excludeLocationMetrics HTTP/1.1\r\n" \
	"Host: "HOSTNAME"\r\n"						\
	"Content-Type: application/json\r\n"				\
	"Authorization: Polte-API " API_KEY "\r\n"			\
	"Connection: close\r\n"						\
	"Content-Length: %d\r\n\r\n"

#define HTTP_REQUEST_BODY						\
	"{"								\
		"\"payload\":{"						\
			"\"gcid\":%d,"					\
			"\"ta\":%d,"					\
			"\"mcc\":%d,"					\
			"\"mnc\":%d,"					\
			"\"tac\":%d,"					\
			"\"earfcn\":[%d,%s],"				\
			"\"pcid\":[%d,%s],"				\
			"\"rsrp\":[%d,%s]"				\
		"}"							\
	"}"

#define HTTP_REQUEST_BODY_NO_NEIGHBORS					\
	"{"								\
		"\"payload\":{"						\
			"\"gcid\":%d,"					\
			"\"ta\":%d,"					\
			"\"mcc\":%d,"					\
			"\"mnc\":%d,"					\
			"\"tac\":%d,"					\
			"\"earfcn\":[%d],"				\
			"\"pcid\":[%d],"				\
			"\"rsrp\":[%d]"					\
		"}"							\
	"}"

/* TLS certificate:
 *	ISRG Root X1
 *	CN=ISRG Root X1
 *	O=Internet Security Research Group
 *	Serial number=91:2b:08:4a:cf:0c:18:a7:53:f6:d6:2e:25:a7:5f:5a
 *	Valid from=Sep 4 00:00:00 2020 GMT
 *	Valid to=Sep 15 16:00:00 2025 GMT
 *	Download url=https://letsencrypt.org/certs/isrgrootx1.pem
 */
static const char tls_certificate[] =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAw\n"
	"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
	"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAw\n"
	"WhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n"
	"RW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
	"AoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cP\n"
	"R5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdx\n"
	"sxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8Zutm\n"
	"NHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxg\n"
	"Z3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG\n"
	"/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMC\n"
	"AYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYB\n"
	"Af8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaA\n"
	"FHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcw\n"
	"AoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRw\n"
	"Oi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQB\n"
	"gt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6W\n"
	"PTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wl\n"
	"ikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQz\n"
	"CkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BIm\n"
	"lJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4\n"
	"avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2\n"
	"yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1O\n"
	"yK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90Ids\n"
	"hCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+\n"
	"HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6Zv\n"
	"MldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqX\n"
	"nLRbwHOoq7hHwg==\n"
	"-----END CERTIFICATE-----\n";

BUILD_ASSERT(sizeof(API_KEY) > 1, "API key must be configured");
BUILD_ASSERT(sizeof(HOSTNAME) > 1, "Hostname must be configured");

static char body[1536];
static char earfcn_array[128];
static char pcid_array[128];
static char rsrp_array[128];

const char *location_service_get_hostname(void)
{
	return HOSTNAME;
}

const char *location_service_get_certificate(void)
{
	return tls_certificate;
}

static int adjust_rsrp(int input)
{
	if (input <= 0) {
		return input - 140;
	}

	return input - 141;
}

int location_service_generate_request(const struct lte_lc_cells_info *cell_data,
				      char *buf, size_t buf_len)
{
	int len;

	if ((cell_data == NULL) || (buf == NULL) || (buf_len == 0)) {
		return -EINVAL;
	}

	if (cell_data->current_cell.id == 0) {
		LOG_WRN("No cells were found");
		return -ENOENT;
	}

	if (cell_data->ncells_count == 0) {
		len = snprintk(body, sizeof(body), HTTP_REQUEST_BODY_NO_NEIGHBORS,
			 cell_data->current_cell.id,
			 cell_data->current_cell.timing_advance / TA_DIVIDER,
			 cell_data->current_cell.mcc,
			 cell_data->current_cell.mnc,
			 cell_data->current_cell.tac,
			 cell_data->current_cell.earfcn,
			 cell_data->current_cell.phys_cell_id,
			 adjust_rsrp(cell_data->current_cell.rsrp));
		if ((len < 0) || (len >= sizeof(body))) {
			LOG_ERR("Too small buffer for HTTP request body");
			return -ENOMEM;
		}

		len = snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", strlen(body), body);
		if ((len < 0) || (len >= buf_len)) {
			LOG_ERR("Too small buffer for HTTP request body");
			return -ENOMEM;
		}

		return 0;
	}

	earfcn_array[0] = '\0';
	pcid_array[0] = '\0';
	rsrp_array[0] = '\0';

	for (size_t i = 0; i < cell_data->ncells_count; i++) {
		int len;
		char element[16];

		len = snprintk(element, sizeof(element), "%d",
			 cell_data->neighbor_cells[i].earfcn);
		if ((len < 0) || (len >= sizeof(element))) {
			LOG_ERR("Too small buffer for EARFCN element");
			return -ENOMEM;
		}

		strncat(earfcn_array, element, sizeof(earfcn_array) - 1);

		len = snprintk(element, sizeof(element), "%d",
			 cell_data->neighbor_cells[i].phys_cell_id);
		if ((len < 0) || (len >= sizeof(element))) {
			LOG_ERR("Too small buffer for physical cell ID element");
			return -ENOMEM;
		}

		strncat(pcid_array, element, sizeof(pcid_array) - 1);

		len = snprintk(element, sizeof(element), "%d",
			 adjust_rsrp(cell_data->neighbor_cells[i].rsrp));
		if ((len < 0) || (len >= sizeof(element))) {
			LOG_ERR("Too small buffer for RSRP element");
			return -ENOMEM;
		}

		strncat(rsrp_array, element, sizeof(rsrp_array) - 1);
	}

	len = snprintk(body, sizeof(body), HTTP_REQUEST_BODY,
		       cell_data->current_cell.id,
		       cell_data->current_cell.timing_advance / TA_DIVIDER,
		       cell_data->current_cell.mcc,
		       cell_data->current_cell.mnc,
		       cell_data->current_cell.tac,
		       cell_data->current_cell.earfcn,
		       earfcn_array,
		       cell_data->current_cell.phys_cell_id,
		       pcid_array,
		       adjust_rsrp(cell_data->current_cell.rsrp),
		       rsrp_array);
	if ((len < 0) || (len >= sizeof(body))) {
		LOG_ERR("Too small buffer for HTTP request body");
		return -ENOMEM;
	}

	len = snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", strlen(body), body);
	if ((len < 0) || (len >= buf_len)) {
		LOG_ERR("Too small buffer for HTTP request");
		return -ENOMEM;
	}

	return 0;
}

int location_service_parse_response(const char *response, struct multicell_location *location)
{
	int err;
	struct cJSON *root_obj, *location_obj, *lat_obj, *lng_obj, *accuracy_obj;
	char *json_start, *http_status;

	if ((response == NULL) || (location == NULL)) {
		return -EINVAL;
	}

	/* The expected response format is the following:
	 *
	 * HTTP/1.1 <HTTP status, 200 OK if successful>
	 * <Additional HTTP header elements>
	 * <\r\n\r\n>
	 * {"location":{"latitude":<double>,"longitude":<double>},"confidence":<integer>}
	 */

	http_status = strstr(response, "HTTP/1.1 200");
	if (http_status == NULL) {
		LOG_ERR("HTTP status was not 200");
		return -1;
	}

	json_start = strstr(response, "\r\n\r\n");
	if (json_start == NULL) {
		LOG_ERR("No payload found");
		return -1;
	}

	root_obj = cJSON_Parse(json_start);
	if (root_obj == NULL) {
		LOG_ERR("Could not parse JSON in payload");
		return -1;
	}

	location_obj = cJSON_GetObjectItemCaseSensitive(root_obj, "location");
	if (location_obj == NULL) {
		LOG_DBG("No 'location' object found");

		err = -1;
		goto clean_exit;
	}

	lat_obj = cJSON_GetObjectItemCaseSensitive(location_obj, "latitude");
	if (lat_obj == NULL) {
		LOG_DBG("No 'latitude' object found");

		err = -1;
		goto clean_exit;
	}

	lng_obj = cJSON_GetObjectItemCaseSensitive(location_obj, "longitude");
	if (lng_obj == NULL) {
		LOG_DBG("No 'longitude' object found");

		err = -1;
		goto clean_exit;
	}

	accuracy_obj = cJSON_GetObjectItemCaseSensitive(location_obj, "confidence");
	if (accuracy_obj == NULL) {
		LOG_DBG("No 'confidence' object found");

		err = -1;
		goto clean_exit;
	}

	location->latitude = lat_obj->valuedouble;
	location->longitude = lng_obj->valuedouble;
	location->accuracy = (double)accuracy_obj->valueint;

	err = 0;

clean_exit:
	cJSON_Delete(root_obj);

	return err;
}
