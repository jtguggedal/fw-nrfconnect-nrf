# Copyright (c) 2023 Nordic Semiconductor ASA.
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

if ((CONFIG_BOARD_THINGY91X_NRF5340_CPUAPP OR CONFIG_BOARD_THINGY91X_NRF5340_CPUAPP_NS)
    AND CONFIG_BOARD_ENABLE_CPUNET)
zephyr_library()
zephyr_library_sources(nrf5340_cpunet_reset.c)
endif()

zephyr_library_sources_ifdef(CONFIG_ALONGSIDE_OB nrf5340_net_ob.c)
