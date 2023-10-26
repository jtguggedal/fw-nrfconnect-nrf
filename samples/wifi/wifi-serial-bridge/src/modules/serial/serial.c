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

#define RX_ESCAPE_CHAR '\n'

LOG_MODULE_REGISTER(serial, 3);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(serial, CONFIG_MQTT_SAMPLE_TRANSPORT_MESSAGE_QUEUE_SIZE);




#define MSG_SIZE CONFIG_MQTT_SAMPLE_PAYLOAD_CHANNEL_STRING_MAX_SIZE

static struct payload payload;
/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq_from_wifi, sizeof(payload), 10, 4);
K_MSGQ_DEFINE(uart_msgq_to_wifi,  sizeof(payload), 10, 4);


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

	payload.string_len = buf_len;

	k_msgq_put(&uart_msgq_to_wifi, &payload, K_NO_WAIT);


}



void print_to_uart(char *buf, uint8_t len)
{
	for (int i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}
static void serial_send(const struct payload *payload)
{
	print_to_uart(payload->string, payload->string_len);
}

static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {

		uart_fifo_read(dev, &c, 1);
		if ((c == RX_ESCAPE_CHAR) && rx_buf_pos > 0) {
			/* terminate string */
			rx_buf[rx_buf_pos++] = c;

			submit_payload(rx_buf, rx_buf_pos);
			LOG_DBG("put on queue");
			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;

		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		} else {
			// ditch all if butter is full and we do not hit frame end
			rx_buf_pos = 0;
		}
	}
}

static void serial_task(void)
{
	int err;
	struct payload payload;
	const struct zbus_channel *chan;

	__ASSERT(device_is_ready(uart_dev), "uart_dev device not ready");

	k_sleep(K_SECONDS(2));

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return;
	}

	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	while (!zbus_sub_wait(&serial, &chan, K_FOREVER)) {
		if (&TRANSPORT_CHAN == chan) {
			err = zbus_chan_read(&TRANSPORT_CHAN, &payload, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			// serial_send(&payload);
			k_msgq_put(&uart_msgq_from_wifi, &payload, K_NO_WAIT);
		}
	}
}

K_THREAD_DEFINE(serial_task_id, 1024, serial_task, NULL, NULL, NULL, 3, 0, 0);

static void serial_out_task(void)
{

	struct payload serial_out = {};
	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return;
	}

	while (k_msgq_get(&uart_msgq_from_wifi, &serial_out, K_FOREVER) == 0) {
		LOG_DBG("sending uart");

		serial_send(&serial_out);
	}
}

K_THREAD_DEFINE(serial_out_task_id, 2048, serial_out_task, NULL, NULL, NULL, 3, 0, 0);

static void serial_in_task(void)
{

	struct payload to_wifi = {};
	int err = 0;

	while (k_msgq_get(&uart_msgq_to_wifi, &to_wifi, K_FOREVER) == 0) {
		LOG_DBG("Submitting payload");

		err = zbus_chan_pub(&PAYLOAD_CHAN, &to_wifi, K_SECONDS(1));
		if (err) {
			LOG_ERR("zbus_chan_pub, error: %d", err);
			SEND_FATAL_ERROR();
		}
	}
}

K_THREAD_DEFINE(serial_in_task_id, 2048, serial_in_task, NULL, NULL, NULL, 3, 0, 0);