/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stddef.h>
#include <modem/nrf_modem_lib_trace.h>


/* Start tracing */
int modem_traces_start(enum nrf_modem_lib_trace_level trace_level);


/* Stop tracing and prepare sending the traces to Memfault */
int modem_traces_stop(void);


/* Dump the traces to UART1 */
void print_traces_to_uart(void);
