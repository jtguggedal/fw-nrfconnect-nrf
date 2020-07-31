#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#
zephyr_library()
zephyr_library_sources(
	src/azure_iot_hub.c
)
zephyr_library_sources_ifdef(
	CONFIG_AZURE_IOT_HUB_DPS src/azure_iot_hub_dps.c
)
zephyr_include_directories(include)
