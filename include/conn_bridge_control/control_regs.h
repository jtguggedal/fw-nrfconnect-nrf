/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CONN_BRIDGE_CONTROL_REGS_H_
#define _CONN_BRIDGE_CONTROL_REGS_H_

/**
 * @file control_regs.h
 *
 * @brief Connectivity bridge control register definitions
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <conn_bridge_control/control_common.h>

DEFINE_CONN_BRIDGE_CTRL_REG(0x0, usb_attached, uint8_t);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _CONN_BRIDGE_CONTROL_REGS_H_ */
