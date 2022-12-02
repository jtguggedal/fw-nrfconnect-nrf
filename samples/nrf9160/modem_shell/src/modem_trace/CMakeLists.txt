#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_traces.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/trace_flash_backend.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/trace_storage.c)
