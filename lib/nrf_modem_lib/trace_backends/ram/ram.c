/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <modem/trace_backend.h>

LOG_MODULE_REGISTER(modem_trace_backend, CONFIG_MODEM_TRACE_BACKEND_LOG_LEVEL);

/* don't put this in noinit, memfault won't be able to capture it */
#define RING_BUF_DECLARE_STATIC(name, size8) \
	BUILD_ASSERT(size8 < RING_BUFFER_MAX_SIZE,\
		RING_BUFFER_SIZE_ASSERT_MSG); \
	static uint8_t _ring_buffer_data_##name[size8]; \
	static struct ring_buf name = { \
		.buffer = _ring_buffer_data_##name, \
		.size = size8 \
	}

RING_BUF_DECLARE_STATIC(ram_trace_buf, CONFIG_NRF_MODEM_LIB_TRACE_BACKEND_RAM_LENGTH);

static trace_backend_processed_cb trace_processed_callback;

int trace_backend_init(trace_backend_processed_cb trace_processed_cb)
{
	trace_processed_callback = trace_processed_cb;
	return 0;
}

int trace_backend_deinit(void)
{
	/* nothing happens */
	return 0;
}

size_t trace_backend_data_size(void)
{
	return ring_buf_size_get(&ram_trace_buf);
}

int trace_backend_read(void *buf, size_t len)
{
	return ring_buf_get(&ram_trace_buf, buf, len);
}

int trace_backend_write(const void *data, size_t len)
{
	__ASSERT_NO_MSG(len <= ring_buf_capacity_get(&ram_trace_buf));

	uint32_t free_space = ring_buf_space_get(&ram_trace_buf);

	if (len > free_space) {
		ring_buf_get(&ram_trace_buf, NULL, len - free_space);
	}

	trace_processed_callback(len);

	return ring_buf_put(&ram_trace_buf, data, len);
}

int trace_backend_clear(void)
{
	ring_buf_reset(&ram_trace_buf);
	return 0;
}

struct nrf_modem_lib_trace_backend trace_backend = {
	.init = trace_backend_init,
	.deinit = trace_backend_deinit,
	.write = trace_backend_write,
	.data_size = trace_backend_data_size,
	.read = trace_backend_read,
	.clear = trace_backend_clear,
};
