#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)
target_include_directories(app PRIVATE ./**/)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/module_common.c)

add_subdirectory_ifdef(CONFIG_CLOUD_MODULE cloud)
add_subdirectory_ifdef(CONFIG_DATA_MODULE data)
add_subdirectory_ifdef(CONFIG_DEBUG_MODULE debug)
add_subdirectory_ifdef(CONFIG_LOCATION_MODULE location)
add_subdirectory_ifdef(CONFIG_LED_CONTROL led)
add_subdirectory_ifdef(CONFIG_MODEM_MODULE modem)
add_subdirectory_ifdef(CONFIG_SENSOR_MODULE sensor)
add_subdirectory_ifdef(CONFIG_UI_MODULE ui)
add_subdirectory_ifdef(CONFIG_UTIL_MODULE util)
