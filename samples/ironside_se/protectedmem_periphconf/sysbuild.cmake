# Copyright (c) 2025 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Apply the same device tree overlay to the UICR and secondary images so every
# image sees the same partition layout
# Also add secondary firmware that boots on integrity failure

include(${ZEPHYR_NRF_MODULE_DIR}/sysbuild/extensions.cmake)

# Add secondary firmware that boots when PROTECTEDMEM integrity check fails
ExternalZephyrProject_Add(
  APPLICATION secondary
  SOURCE_DIR ${APP_DIR}/secondary
)

# The partition layout is board specific, and global to the build: the default
# image picks its overlay up from boards/ automatically, the other images need
# it applied here.
string(REGEX REPLACE "^/" "" board_qualifiers "${BOARD_QUALIFIERS}")
string(REPLACE "/" "_" board_overlay_name "${BOARD}/${board_qualifiers}")
set(board_overlay ${APP_DIR}/boards/${board_overlay_name}.overlay)

if(NOT EXISTS ${board_overlay})
  message(FATAL_ERROR
    "This sample needs a partition layout overlay for the target board, but "
    "${board_overlay} does not exist."
  )
endif()

add_overlay_dts(uicr ${board_overlay})
add_overlay_dts(secondary ${board_overlay})
