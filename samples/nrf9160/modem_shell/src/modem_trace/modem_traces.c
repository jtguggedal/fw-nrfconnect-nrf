/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <sys/errno.h>
#include <modem/trace_backend.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <modem/nrf_modem_lib.h>
#include <modem/nrf_modem_lib_trace.h>
#include <nrf_modem.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault/core/custom_data_recording.h>

#include "mosh_print.h"

#include "modem_traces.h"
#include "trace_storage.h"

LOG_MODULE_REGISTER(modem_traces, 4);

#define UART1_DT_NODE DT_NODELABEL(uart1)

static const struct device *const uart_dev = DEVICE_DT_GET(UART1_DT_NODE);

static const char *mimetypes[] = { MEMFAULT_CDR_BINARY };
static sMemfaultCdrMetadata trace_recording_metadata = {
	.start_time.type = kMemfaultCurrentTimeType_Unknown,
	.mimetypes = mimetypes,
	.num_mimetypes = 1,
	.collection_reason = "modem traces",
};


extern uint32_t traces_size; /* Updated by trace_storage.c, lives in .noinit RAM */
static bool has_modem_traces;

static bool has_cdr_cb(sMemfaultCdrMetadata *metadata)
{
	LOG_INF("has_cdr_cb: true");

	if (!has_modem_traces) {
		return false;
	}

	*metadata = trace_recording_metadata;

	return true;
}

static bool read_data_cb(uint32_t offset, void *data, size_t data_len)
{
	int err = trace_storage_read(data, data_len, offset);

	if (err < 0) {
		LOG_ERR("Error reading modem traces: %d", err);

		return false;
	}

	LOG_INF("Modem traces read, offset: %d, length: %d", offset, data_len);

	return true;
}

static void mark_cdr_read_cb(void)
{
	LOG_INF("mark_cdr_read_cb");

	has_modem_traces = false;
}

static sMemfaultCdrSourceImpl s_my_custom_data_recording_source = {
	.has_cdr_cb = has_cdr_cb,
	.read_data_cb = read_data_cb,
	.mark_cdr_read_cb = mark_cdr_read_cb,
};

static void print_uart1(char *buf, int len)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("uart1 device not found/ready!");
		return;
	}
	for (int i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}


int modem_traces_start(enum nrf_modem_lib_trace_level trace_level)
{
	int err;

	LOG_INF("Starting modem traces");

	memfault_cdr_register_source(&s_my_custom_data_recording_source);

	err = nrf_modem_lib_trace_level_set(trace_level);
	if (err) {
		LOG_ERR("Failed to enable modem traces");
		return err;
	}

	trace_recording_metadata.duration_ms = k_uptime_get_32();

        return 0;
}

int modem_traces_stop(void)
{
	int err;

	LOG_INF("Stopping modem traces");

	err = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_OFF);
	if (err) {
		LOG_ERR("Failed to turn off modem traces");
                return err;
	} else {
		LOG_INF("Turned off modem traces");
	}

	trace_recording_metadata.duration_ms =
		k_uptime_get_32() - trace_recording_metadata.duration_ms;

	/* Changing the trace level to off will produce some traces, so sleep long enough to
	 * receive those as well.
	 */
	k_sleep(K_SECONDS(1));

	err = trace_storage_flush();
	__ASSERT_NO_MSG(err == 0);

	has_modem_traces = true;

	trace_recording_metadata.data_size_bytes = traces_size;

        return 0;
}

void dump_traces_to_uart(void)
{
	const size_t READ_BUF_SIZE = 1024;
	uint8_t read_buf[READ_BUF_SIZE];
	int ret = READ_BUF_SIZE;
	size_t read_offset = 0;

	/* Read out the trace data from flash */
	while (ret == READ_BUF_SIZE) {
		ret = trace_storage_read(read_buf, READ_BUF_SIZE, read_offset);
		if (ret < 0) {
			LOG_ERR("Error reading modem traces: %d", ret);
			break;
		}
		read_offset += ret;
		print_uart1(read_buf, ret);
	}

	LOG_INF("Total trace bytes dumped from flash to UART1: %d", read_offset);
}


/* Shell */

static const char shell_usage_str[] =
	"Usage: modem_trace <command> [options]\n"
	"\n"
	"<command> is one of the following:\n"
	"  start:       Start modem tracing. \n"
	"               Optional: <level [1-5]>\n"
	"  stop:        Stop modem tracing and prepare for sending traces to Memfault.\n"
        "  dump_uart:   Dump the stored modem traces to UART1.\n";

static void shell_print_usage(const struct shell *shell)
{
	shell_print(shell, "%s", shell_usage_str);
}

static int modem_trace_shell_start(size_t argc, char **argv)
{
        enum nrf_modem_lib_trace_level level =
                argc == 1 ? (enum nrf_modem_lib_trace_level)atoi(argv[1]) :
                NRF_MODEM_LIB_TRACE_LEVEL_FULL;

        __ASSERT_NO_MSG((level >= NRF_MODEM_LIB_TRACE_LEVEL_OFF) && (level <= NRF_MODEM_LIB_TRACE_LEVEL_LTE_AND_IP));

        return modem_traces_start(level);
}

static int modem_trace_shell(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print_usage(shell);
		return 0;
	}

	/* Command = argv[1] */
	if (!strcmp(argv[1], "start")) {
		return modem_trace_shell_start(argc, argv);
	} else if (!strcmp(argv[1], "stop")) {
		return modem_traces_stop();
	} else if (!strcmp(argv[1], "dump_uart")) {
                dump_traces_to_uart();

                return 0;
	}

        mosh_error("Unsupported command = %s\n", argv[1]);
        shell_print_usage(shell);

        return -EINVAL;

}

SHELL_CMD_REGISTER(modem_trace, NULL, "Commands for modem tracing.", modem_trace_shell);
