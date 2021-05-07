#
# Copyright (c) 2021 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)

target_sources_ifdef(CONFIG_MULTICELL_SERVICE_HERE
        app PRIVATE here_integration.c)

target_sources_ifdef(CONFIG_MULTICELL_SERVICE_SKYHOOK
        app PRIVATE skyhook_integration.c)
