/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __ETB_TRACE_H
#define __ETB_TRACE_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETB_BUFFER_SIZE			KB(2)

void etb_trace_start(void);

void etb_trace_stop(void);

size_t etb_data_get(volatile uint32_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __ETB_TRACE_H */
