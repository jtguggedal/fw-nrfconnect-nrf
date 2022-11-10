#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources_ifdef(CONFIG_AWS_IOT app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_integration.c)

target_sources_ifdef(CONFIG_AZURE_IOT_HUB app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/azure_iot_hub_integration.c)

target_sources_ifdef(CONFIG_NRF_CLOUD_MQTT app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nrf_cloud_integration.c)

target_sources_ifdef(CONFIG_LWM2M_INTEGRATION app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_integration.c)

target_sources_ifdef(CONFIG_LWM2M_INTEGRATION app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/configuration_object.c)
