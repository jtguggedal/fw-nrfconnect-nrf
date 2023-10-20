/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

LOG_MODULE_REGISTER(serial, 4);

#define BUF_SIZE 1024

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(serial, CONFIG_MQTT_SAMPLE_TRANSPORT_MESSAGE_QUEUE_SIZE);

static void submit_payload_work_fn(struct k_work *work);

static K_MEM_SLAB_DEFINE(uart_slab, BUF_SIZE, 2, 4);
static K_WORK_DELAYABLE_DEFINE(submit_payload_work, submit_payload_work_fn);

static struct payload payload;

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

static void submit_payload(const char *buf, const size_t buf_len)
{
	int len;

	len = snprintk(payload.string, sizeof(payload.string), "%s", buf);
	if ((len < 0) || (len >= sizeof(payload))) {
		LOG_ERR("Failed to construct message, error: %d", len);
		SEND_FATAL_ERROR();
		return;
	}

	// memcpy(payload.string, buf, buf_len);

	// payload.string[buf_len] = 0;

	payload.string_len = buf_len;

	k_work_schedule(&submit_payload_work, K_NO_WAIT);
}

static void submit_payload_work_fn(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

        LOG_DBG("Submitting payload");

	err = zbus_chan_pub(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct device *uart = user_data;
	int err;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_INF("Tx sent %d bytes", evt->data.tx.len);
		break;

	case UART_TX_ABORTED:
		LOG_ERR("Tx aborted");
		break;

	case UART_RX_RDY:

		LOG_INF("UART_RX_RDY: %p, offset %d", (void *)evt->data.rx.buf, evt->data.rx.offset);
		LOG_INF("Received %d bytes", evt->data.rx.len);
		LOG_HEXDUMP_INF(&evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len, "");

		submit_payload(&evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len);
		break;

	case UART_RX_BUF_REQUEST:
	{
		uint8_t *buf;

		err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
		__ASSERT(err == 0, "Failed to allocate slab");

		err = uart_rx_buf_rsp(uart, buf, BUF_SIZE);
		__ASSERT(err == 0, "Failed to provide new buffer");
		LOG_INF("UART_RX_BUF_REQUEST: %p", (void *)buf);
		break;
	}

	case UART_RX_BUF_RELEASED:
		LOG_INF("UART_RX_BUF_RELEASED: %p", (void *)evt->data.rx_buf.buf);
		k_mem_slab_free(&uart_slab, (void *)evt->data.rx_buf.buf);
		break;

	case UART_RX_DISABLED:
		LOG_ERR("UART_RX_DISABLED");
		break;

	case UART_RX_STOPPED:
		LOG_WRN("UART_RX_STOPPED reason %d", evt->data.rx_stop.reason);
		break;
	}
}

static void serial_send(const struct payload *payload)
{
        int err;

        err = uart_tx(uart_dev, payload->string, payload->string_len, 10000);
	__ASSERT(err == 0, "Failed to initiate transmission");
}

static void async_setup(void)
{
	int err;
	uint8_t *buf;


	err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
	__ASSERT(err == 0, "Failed to alloc slab");

	err = uart_callback_set(uart_dev, uart_callback, (void *)uart_dev);
	__ASSERT(err == 0, "Failed to set callback");

	err = uart_rx_enable(uart_dev, buf, BUF_SIZE, 10000);
	__ASSERT(err == 0, "Failed to enable RX");

        LOG_INF("Started UART RX");
}


static void serial_task(void)
{
	int err;
	struct payload payload;
	const struct zbus_channel *chan;

	__ASSERT(device_is_ready(uart_dev), "uart_dev device not ready");

	async_setup();

	k_sleep(K_SECONDS(2));

	while (!zbus_sub_wait(&serial, &chan, K_FOREVER)) {
		if (&TRANSPORT_CHAN == chan) {
			err = zbus_chan_read(&TRANSPORT_CHAN, &payload, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			serial_send(&payload);
		}
	}
}

K_THREAD_DEFINE(serial_task_id,
		1024,
		serial_task, NULL, NULL, NULL, 3, 0, 0);
