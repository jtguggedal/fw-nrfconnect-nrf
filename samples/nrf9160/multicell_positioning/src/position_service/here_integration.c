/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h>

#include "position_service.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(multicell_positioning_here, CONFIG_MULTICELL_POSITIONING_LOG_LEVEL);

#define API_APP_CODE 	CONFIG_MULTICELL_HERE_APP_CODE
#define API_APP_ID	CONFIG_MULTICELL_HERE_APP_ID
#define HOSTNAME	CONFIG_MULTICELL_HERE_HOSTNAME

#define HTTP_REQUEST_HEADER						\
	"POST /positioning/v1/locate?app_code="API_APP_CODE"&app_id="API_APP_ID" HTTP/1.1\r\n" \
	"Host: "HOSTNAME"\r\n"					        \
	"Content-Type: application/json\r\n"				\
	"Content-Length: %d\r\n\r\n"

#define HTTP_REQUEST_BODY						\
	"{"								\
		"\"lte\":["						\
			"{"						\
				"\"mcc\": %d,"				\
				"\"mnc\": %d,"				\
				"\"cid\": %d,"				\
				"\"nmr\":["				\
					"%s"				\
				"]"					\
			"}"						\
		"]"							\
	"}"

#define HTTP_REQUEST_BODY_NO_NEIGHBORS					\
	"{"								\
		"\"lte\":["						\
			"{"						\
				"\"mcc\": %d,"				\
				"\"mnc\": %d,"				\
				"\"cid\": %d"				\
			"}"						\
		"]"							\
	"}"

#define HTTP_REQUEST_BODY_NEIGHBOR_ELEMENT				\
        "{"								\
            "\"earfcn\": %d,"						\
            "\"pci\": %d"						\
        "}"

static const char tls_certificate[] =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIETjCCAzagAwIBAgINAe5fIh38YjvUMzqFVzANBgkqhkiG9w0BAQsFADBMMSAw\n"
        "HgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEGA1UEChMKR2xvYmFs\n"
        "U2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjAeFw0xODExMjEwMDAwMDBaFw0yODEx\n"
        "MjEwMDAwMDBaMFAxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52\n"
        "LXNhMSYwJAYDVQQDEx1HbG9iYWxTaWduIFJTQSBPViBTU0wgQ0EgMjAxODCCASIw\n"
        "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKdaydUMGCEAI9WXD+uu3Vxoa2uP\n"
        "UGATeoHLl+6OimGUSyZ59gSnKvuk2la77qCk8HuKf1UfR5NhDW5xUTolJAgvjOH3\n"
        "idaSz6+zpz8w7bXfIa7+9UQX/dhj2S/TgVprX9NHsKzyqzskeU8fxy7quRU6fBhM\n"
        "abO1IFkJXinDY+YuRluqlJBJDrnw9UqhCS98NE3QvADFBlV5Bs6i0BDxSEPouVq1\n"
        "lVW9MdIbPYa+oewNEtssmSStR8JvA+Z6cLVwzM0nLKWMjsIYPJLJLnNvBhBWk0Cq\n"
        "o8VS++XFBdZpaFwGue5RieGKDkFNm5KQConpFmvv73W+eka440eKHRwup08CAwEA\n"
        "AaOCASkwggElMA4GA1UdDwEB/wQEAwIBhjASBgNVHRMBAf8ECDAGAQH/AgEAMB0G\n"
        "A1UdDgQWBBT473/yzXhnqN5vjySNiPGHAwKz6zAfBgNVHSMEGDAWgBSP8Et/qC5F\n"
        "JK5NUPpjmove4t0bvDA+BggrBgEFBQcBAQQyMDAwLgYIKwYBBQUHMAGGImh0dHA6\n"
        "Ly9vY3NwMi5nbG9iYWxzaWduLmNvbS9yb290cjMwNgYDVR0fBC8wLTAroCmgJ4Yl\n"
        "aHR0cDovL2NybC5nbG9iYWxzaWduLmNvbS9yb290LXIzLmNybDBHBgNVHSAEQDA+\n"
        "MDwGBFUdIAAwNDAyBggrBgEFBQcCARYmaHR0cHM6Ly93d3cuZ2xvYmFsc2lnbi5j\n"
        "b20vcmVwb3NpdG9yeS8wDQYJKoZIhvcNAQELBQADggEBAJmQyC1fQorUC2bbmANz\n"
        "EdSIhlIoU4r7rd/9c446ZwTbw1MUcBQJfMPg+NccmBqixD7b6QDjynCy8SIwIVbb\n"
        "0615XoFYC20UgDX1b10d65pHBf9ZjQCxQNqQmJYaumxtf4z1s4DfjGRzNpZ5eWl0\n"
        "6r/4ngGPoJVpjemEuunl1Ig423g7mNA2eymw0lIYkN5SQwCuaifIFJ6GlazhgDEw\n"
        "fpolu4usBCOmmQDo8dIm7A9+O4orkjgTHY+GzYZSR+Y0fFukAj6KYXwidlNalFMz\n"
        "hriSqHKvoflShx8xpfywgVcvzfTO3PYkz6fiNJBonf6q8amaEsybwMbDqKWwIX7e\n"
        "SPY=\n"
        "-----END CERTIFICATE-----\n";

BUILD_ASSERT(sizeof(API_APP_CODE) > 1, "App code must be configured");
BUILD_ASSERT(sizeof(API_APP_ID) > 1, "App ID must be configured");
BUILD_ASSERT(sizeof(HOSTNAME) > 1, "Hostname must be configured");

const char *position_service_get_hostname(void)
{
        return HOSTNAME;
}

const char *position_service_get_certificate(void)
{
        return tls_certificate;
}

int position_service_generete_request(struct lte_lc_cells_info *cell_data,
                                      char *buf, size_t buf_len)
{
	if (cell_data->current_cell.id == 0) {
		LOG_WRN("No cells were found");
		return -ENOENT;
	}

	char body[256];
	char neighbors[256];

	if (cell_data->ncells_count == 0) {
		printk("*** No neighbor cells found ***\n");

		snprintk(body, sizeof(body), HTTP_REQUEST_BODY_NO_NEIGHBORS,
			 cell_data->current_cell.mcc,
			 cell_data->current_cell.mnc,
			 cell_data->current_cell.id);

		snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", strlen(body), body);

		return 0;
	}

	*neighbors = 0;

	for (size_t i = 0; i < cell_data->ncells_count; i++) {
		char element[50];
		snprintk(element, sizeof(element), HTTP_REQUEST_BODY_NEIGHBOR_ELEMENT "%s",
			 cell_data->neighbor_cells[i].earfcn,
			 cell_data->neighbor_cells[i].phys_cell_id,
			 i + 1 < cell_data->ncells_count ? "," : "");
		strcat(neighbors, element);
	}

	snprintk(body, sizeof(body), HTTP_REQUEST_BODY,
			cell_data->current_cell.mcc,
			cell_data->current_cell.mnc,
			cell_data->current_cell.id,
			neighbors);

	snprintk(buf, buf_len, HTTP_REQUEST_HEADER "%s", strlen(body), body);

        return 0;
}
