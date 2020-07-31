/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief Azure IoT Hub Device Provisioning Service.
 */

#ifndef AZUE_IOT_HUB_DPS__
#define AZUE_IOT_HUB_DPS__

#include <stdio.h>
#include <net/azure_iot_hub.h>

#ifdef __cplusplus
extern "C" {
#endif

enum dps_reg_state {
	DPS_STATE_NOT_STARTED,
	DPS_STATE_REGISTERING,
	DPS_STATE_REGISTERED,
	DPS_STATE_FAILED,
};

/* @brief The handler will be called when registration is done, be it
 * successfully or if it fails.
 */
typedef void (*dps_handler_t)(enum dps_reg_state state);

int dps_init(struct mqtt_client *mqtt_client, dps_handler_t handler);

/* @brief Parse incoming MQTT message to see if it's DPS related and process
 *	  accordingly if it is.
 *
 * @retval true The message was DPS-related and is consumed.
 * @retval false The message has not DPS-related.
 */
bool dps_process_message(struct azure_iot_hub_evt *evt);

/* @brief Start Azure device provisioning service.
 *
 * @retval 0 if Azure device provisioning was successfully started.
 * @retval -EFAULT if settings could not be loaded from flash
 * @retval -EALREADY if the device has already been registered and is ready to
 *	    connect to an IoT hub.
 */
int dps_start(void);

bool dps_reg_in_progress(void);

int dps_send_reg_request(void);

int dps_subscribe(void);

char *dps_reg_id_get(void);

char *dps_hostname_get(void);

int dps_reg_id_set(const char *id, size_t id_len);

enum dps_reg_state dps_get_reg_state(void);

/**
 *@}
 */

#ifdef __cplusplus
}
#endif

#endif /* AZUE_IOT_HUB_DPS__ */
