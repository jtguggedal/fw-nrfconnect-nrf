#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)
add_subdirectory(cloud_integration)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cloud_module.c)
