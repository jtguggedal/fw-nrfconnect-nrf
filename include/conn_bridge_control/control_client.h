/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CONN_BRIDGE_CONTROL_CLIENT_H_
#define _CONN_BRIDGE_CONTROL_CLIENT_H_

/**
 * @file control_client.h
 *
 * @brief Connectivity bridge control client
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <zephyr/device.h>

void conn_bridge_control_client_init(const struct device *dev);
int conn_bridge_control_get_usb_attached(bool *attached);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _CONN_BRIDGE_CONTROL_CLIENT_H_ */
