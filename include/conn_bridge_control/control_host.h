/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CONN_BRIDGE_CONTROL_HOST_H_
#define _CONN_BRIDGE_CONTROL_HOST_H_

/**
 * @file control_host.h
 *
 * @brief Connectivity bridge control host
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <zephyr/device.h>

void conn_bridge_control_host_init(const struct device *dev);
void conn_bridge_control_set_usb_attached(bool attached);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _CONN_BRIDGE_CONTROL_HOST_H_ */
