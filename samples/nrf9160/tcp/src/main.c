/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <net/socket.h>
#include <stdio.h>
#include <string.h>
#include <lte_lc.h>

#define TCP_SERVER_IPV4 	"xxx.xxx.xxx.xxx"
#define TCP_PORT 		nnnn
#define RECV_BUF_SIZE 		((80 * 24) + 1)

static char recv_buf[RECV_BUF_SIZE];

void main(void)
{
	int err;
	size_t ret;
	int client_fd;
	struct sockaddr_in server = { 0 };
	struct sockaddr_in local_addr = { 0 };
	char msg[] = "hello";

	printk("TCP client started\n");

	printk("Setting up LTE connection\n");

	err = lte_lc_init_and_connect();
	if (err) {
		printk("LTE link could not be established, error: %d\n",
			err);
		return;
	}

	printk("LTE connected\n");

	server.sin_family = AF_INET;
	server.sin_port = htons(TCP_PORT);

	err = inet_pton(AF_INET, TCP_SERVER_IPV4, &server.sin_addr);
	if (err != 1) {
		printk("inet_pton failed, errno: %d\n", errno);
		return;
	}

	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(0);
	local_addr.sin_addr.s_addr = 0;

	client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd <= 0) {
		printk("socket() failed, errno: %d\n", errno);
		return;
	}

	printk("client_fd: %d\n\r", client_fd);

	err = bind(client_fd, (struct sockaddr *)&local_addr,
		   sizeof(local_addr));
	if (err) {
		printk("bind failed, errno: %d\n\r", errno);
		goto exit;
	}

	err = connect(client_fd, (struct sockaddr *)&server,
		      sizeof(struct sockaddr_in));
	if (err) {
		printk("connect failed, errno: %d\n\r", errno);
		goto exit;
	}


	err = send(client_fd, msg, strlen(msg), 0);
	if (err < 0) {
		printk("Failed to send data, errno: %d\n", errno);
		goto exit;
	}

	printk("Data sent successfully, waiting for response...\n");

	ret = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);
	if (ret > 0 && ret < RECV_BUF_SIZE) {
		recv_buf[ret] = 0;
		printk("Response: %s\n", recv_buf);
	}

exit:
	(void)close(client_fd);
}
