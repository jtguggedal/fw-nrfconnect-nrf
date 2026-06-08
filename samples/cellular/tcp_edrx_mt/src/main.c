/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

LOG_MODULE_REGISTER(tcp_edrx_mt, CONFIG_TCP_TEST_LOG_LEVEL);

static int sock = -1;
static struct sockaddr_in server_addr;

K_SEM_DEFINE(lte_connected_sem, 0, 1);

#if CONFIG_TCP_TEST_KEEPALIVE_SECONDS > 0
static struct k_work_delayable keepalive_work;
#endif

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}

		LOG_INF("Network registration: %s",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"home" : "roaming");

		k_sem_give(&lte_connected_sem);

		break;
	case LTE_LC_EVT_EDRX_UPDATE:
		/* Printed as integer milliseconds to avoid pulling in float printf. */
		LOG_INF("eDRX granted: eDRX %d ms, PTW %d ms",
			(int)(evt->edrx_cfg.edrx * 1000.0f),
			(int)(evt->edrx_cfg.ptw * 1000.0f));

		break;
	case LTE_LC_EVT_RRC_UPDATE:
		/* Maps to +CSCON: connected/idle — useful to align with paging. */
		LOG_INF("RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"connected" : "idle");

		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_INF("Cell update: Cell ID %d, TAC %d", evt->cell.id, evt->cell.tac);

		break;
	default:
		break;
	}
}

static int server_connect(void)
{
	int err;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create TCP socket, errno %d", errno);

		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (err) {
		LOG_ERR("Failed to connect, errno %d", errno);
		close(sock);

		sock = -1;

		return -errno;
	}

	LOG_INF("TCP connected to %s:%d", CONFIG_TCP_TEST_SERVER_ADDRESS,
		CONFIG_TCP_TEST_SERVER_PORT);

	return 0;
}

#if CONFIG_TCP_TEST_KEEPALIVE_SECONDS > 0
static void keepalive_fn(struct k_work *work)
{
	if (sock >= 0) {
		const char ka[] = "ka";
		int err = send(sock, ka, strlen(ka), 0);

		LOG_INF("TX keepalive (%d)", err);
	}

	k_work_schedule(&keepalive_work, K_SECONDS(CONFIG_TCP_TEST_KEEPALIVE_SECONDS));
}
#endif

int main(void)
{
	int err;
	static uint8_t buf[CONFIG_TCP_TEST_RECV_BUF_SIZE];

	LOG_INF("TCP eDRX MT-data test started");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize modem library, error: %d", err);

		return -1;
	}

	LOG_INF("Connecting to LTE-M (eDRX 61.44 s / PTW 2.56 s requested)...");

	err = lte_lc_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Failed to connect to LTE network, error: %d", err);

		return -1;
	}

	k_sem_take(&lte_connected_sem, K_FOREVER);

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(CONFIG_TCP_TEST_SERVER_PORT);

	inet_pton(AF_INET, CONFIG_TCP_TEST_SERVER_ADDRESS, &server_addr.sin_addr);

#if CONFIG_TCP_TEST_KEEPALIVE_SECONDS > 0
	k_work_init_delayable(&keepalive_work, keepalive_fn);
#endif

	/* (Re)connect and receive forever. recv() blocks, so the modem is free to
	 * enter eDRX sleep between downlink packets; it returns when data arrives.
	 */
	while (1) {
		if (sock < 0) {
			if (server_connect() != 0) {
				LOG_WRN("Reconnect in 10 s");
				k_sleep(K_SECONDS(10));

				continue;
			}
#if CONFIG_TCP_TEST_KEEPALIVE_SECONDS > 0
			k_work_schedule(&keepalive_work,
					K_SECONDS(CONFIG_TCP_TEST_KEEPALIVE_SECONDS));
#endif
		}

		int len = recv(sock, buf, sizeof(buf) - 1, 0);

		if (len > 0) {
			buf[len] = '\0';

			LOG_INF("RX %d bytes: %s", len, (char *)buf);

			/* Confirm receipt by echoing the payload back. The server waits
			 * for this uplink before sending the next package, and the
			 * send->confirm round trip is what reveals the MT-delivery delay.
			 */
			if (send(sock, buf, len, 0) < 0) {
				LOG_ERR("Failed to send confirmation, errno %d", errno);
			}
		} else if (len == 0) {
			LOG_WRN("Peer closed connection; reconnecting");
			close(sock);

			sock = -1;
		} else {
			LOG_ERR("recv error, errno %d; reconnecting", errno);
			close(sock);

			sock = -1;

			k_sleep(K_SECONDS(2));
		}
	}
}
