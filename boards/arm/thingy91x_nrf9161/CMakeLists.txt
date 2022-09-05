# Kconfig - nRF91 THINGY91 board configuration
#
# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

if(CONFIG_BOARD_THINGY91X_NRF9161 AND NOT DEFINED CONFIG_MCUBOOT AND NOT DEFINED CONFIG_IS_SECURE_BOOTLOADER)
	zephyr_library()
	zephyr_library_sources(npm1300_init.c)
endif()

if(CONFIG_BOARD_THINGY91X_NRF9161_NS)
	# If TF-M is used, the ADP5360 configuration must be done in non-secure
	# as we can't instruct TF-M to run it.
	# If SPM is used instead, it will run the ADP5360 configuration in secure.
	if(CONFIG_BUILD_WITH_TFM)
		zephyr_library()
		zephyr_library_sources(npm1300_init.c)
	endif()

	# Use static partition layout to ensure the partition layout remains
	# unchanged after DFU. This needs to be made globally available
	# because it is used in other CMake files.

	set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_LIST_DIR}/thingy91x_pm_static.yml CACHE INTERNAL "")
endif()
