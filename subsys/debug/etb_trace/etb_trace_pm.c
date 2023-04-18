/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/wait_q.h>
#include <debug/etb_trace.h>

static bool trace_stop;

void sys_clock_idle_exit(void)
{
	if (trace_stop) {
		etb_trace_start();

                trace_stop = false;
	}
}

bool z_arm_on_enter_cpu_idle(void)
{
	trace_stop = _kernel.idle > k_ms_to_ticks_ceil32(CONFIG_ETB_TRACE_PM_MIN_IDLE_TIME_MS);

	if (trace_stop) {
		etb_trace_stop();
		NRF_TAD_S->TASKS_CLOCKSTOP = TAD_TASKS_CLOCKSTOP_TASKS_CLOCKSTOP_Msk;
	}

	return true;
}
