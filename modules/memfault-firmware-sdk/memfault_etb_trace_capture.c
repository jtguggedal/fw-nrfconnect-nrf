/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <debug/etb_trace.h>
#include <memfault/panics/platform/coredump.h>
#include <zephyr/wait_q.h>
#include <zephyr/pm/pm.h>

#define MIN_IDLE_TIME_TRACE_SHUTDOWN_MS		1000
#define ETB_BUFFER_VALID_MAGIC 			0xDEADBEEF

static volatile uint32_t etb_buf[ETB_BUFFER_SIZE / 4];
static volatile uint32_t etb_buf_valid;

void memfault_platform_fault_handler(const sMfltRegState *regs,
                                     eMemfaultRebootReason reason)
{
	ARG_UNUSED(regs);
	ARG_UNUSED(reason);

	etb_trace_stop();
	etb_data_get(etb_buf, ARRAY_SIZE(etb_buf));

	etb_buf_valid = ETB_BUFFER_VALID_MAGIC;
}


static bool trace_action;

void sys_clock_idle_exit(void)
{

	if (trace_action) {
		etb_trace_start();
	}
}

static void idle_override()
{
	while (1) {
		(void) arch_irq_lock();

		_kernel.idle = z_get_next_timeout_expiry();

		trace_action = _kernel.idle > k_ms_to_ticks_ceil32(MIN_IDLE_TIME_TRACE_SHUTDOWN_MS);

		if (trace_action) {
			etb_trace_stop();
			NRF_TAD_S->TASKS_CLOCKSTOP = TAD_TASKS_CLOCKSTOP_TASKS_CLOCKSTOP_Msk;
		}

		k_cpu_idle();

		if (trace_action) {
			etb_trace_start();
		}
	}
}

K_THREAD_DEFINE(idle_override_thread, 1024, hacky_idle, NULL, NULL, NULL, K_IDLE_PRIO - 1, 0, 0);
