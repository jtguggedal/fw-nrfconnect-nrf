/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <dk_buttons_and_leds.h>

#include "position_service/position_service.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(multicell_positioning, CONFIG_MULTICELL_POSITIONING_LOG_LEVEL);

#define HTTPS_PORT	443
#define TLS_SEC_TAG	101
#define RECV_BUF_SIZE	512

BUILD_ASSERT(!IS_ENABLED(CONFIG_MULTICELL_SERVICE_NONE),
	"A positioning service must be enabled");

static K_SEM_DEFINE(lte_connected, 0, 1);
static K_SEM_DEFINE(cell_data_ready, 0, 1);
static struct k_work_delayable periodic_search_work;
static struct lte_lc_ncell neighbor_cells[17];
static char http_request[1024];
static char recv_buf[RECV_BUF_SIZE];
static struct lte_lc_cells_info cell_data = {
	.neighbor_cells = neighbor_cells,
};

static int cert_provision(void)
{
	int err;
	bool exists;
	uint8_t unused;
	const char *certificate = position_service_get_certificate();

	if (certificate == NULL) {
		LOG_ERR("No certificate was provided by the positioning service");
		return -EFAULT;
	}

	err = modem_key_mgmt_exists(TLS_SEC_TAG,
				    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				    &exists, &unused);
	if (err) {
		LOG_ERR("Failed to check for certificates err %d", err);
		return err;
	}

	if (exists) {
		/* For the sake of simplicity we delete what is provisioned
		 * with our security tag and reprovision our certificate.
		 */
		err = modem_key_mgmt_delete(TLS_SEC_TAG,
					    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
		if (err) {
			LOG_ERR("Failed to delete existing certificate, err %d", err);
		}
	}



	LOG_INF("Provisioning certificate");

	/*  Provision certificate to the modem */
	err = modem_key_mgmt_write(TLS_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   certificate, strlen(certificate));
	if (err) {
		LOG_ERR("Failed to provision certificate, err %d", err);
		return err;
	}

	return 0;
}

static int tls_setup(int fd)
{
	int err;
	int verify;

	/* Security tag that we have provisioned the certificate with */
	const sec_tag_t tls_sec_tag[] = {
		TLS_SEC_TAG,
	};

	/* Set up TLS peer verification */
	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = REQUIRED;

	err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (err) {
		LOG_ERR("Failed to setup peer verification, err %d", errno);
		return err;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
	if (err) {
		LOG_ERR("Failed to setup TLS sec tag, err %d", errno);
		return err;
	}

	return 0;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}

		LOG_INF("Network registration status: %s",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM parameter update: TAU: %d, Active time: %d",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			LOG_DBG("%s", log_strdup(log_buf));
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("LTE cell changed: Cell ID: %d, Tracking area: %d",
			evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		LOG_INF("Active LTE mode changed: %s",
			evt->lte_mode == LTE_LC_LTE_MODE_NONE ? "None" :
			evt->lte_mode == LTE_LC_LTE_MODE_LTEM ? "LTE-M" :
			evt->lte_mode == LTE_LC_LTE_MODE_NBIOT ? "NB-IoT" :
			"Unknown");
		break;
	case LTE_LC_EVT_NEIGHBOR_CELL_MEAS:
		LOG_INF("Neighbor cell measurements received");

		/* Copy current and neighbor cell information. */
		memcpy(&cell_data, &evt->cells_info, sizeof(struct lte_lc_cells_info));
		memcpy(neighbor_cells, evt->cells_info.neighbor_cells,
			sizeof(struct lte_lc_ncell) * cell_data.ncells_count);
		cell_data.neighbor_cells = neighbor_cells;

		k_sem_give(&cell_data_ready);
		break;
	default:
		break;
	}
}

static void lte_connect(void)
{
	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		int err;

		err = lte_lc_init_and_connect_async(lte_handler);
		if (err) {
			LOG_ERR("Modem could not be configured, error: %d",
				err);
			return;
		}

		/* Check LTE events of type LTE_LC_EVT_NW_REG_STATUS in
		 * lte_handler() to determine when the LTE link is up.
		 */
	}
}

static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	if (has_changed & button_states & DK_BTN1_MSK) {
		int err = lte_lc_neighbor_cell_measurement();

		if (err) {
			LOG_ERR("Failed to initiate neighbor cell measurements");
		}
	}
}

static void periodic_search_work_fn(struct k_work *work)
{
	int err = lte_lc_neighbor_cell_measurement();

	if (err) {
		LOG_ERR("Failed to initiate neighbor cell measurements");
	}

	k_work_reschedule(k_work_delayable_from_work(work),
		K_SECONDS(CONFIG_MULTICELL_PERIODIC_SEARCH_INTERVAL));
}

static void send_request(const char *request)
{
	int err, fd, bytes;
	size_t off;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	size_t len = strlen(request);

	printk("Preparing to send request: \n\n%s\n\n", request);

	err = getaddrinfo(position_service_get_hostname(), NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno);
		return;
	}

	((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTPS_PORT);

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
	if (fd == -1) {
		printk("Failed to open socket!\n");
		goto clean_up;
	}

	/* Setup TLS socket options */
	err = tls_setup(fd);
	if (err) {
		goto clean_up;
	}

	LOG_INF("Connecting to %s", position_service_get_hostname());

	err = connect(fd, res->ai_addr, sizeof(struct sockaddr_in));
	if (err) {
		LOG_ERR("connect() failed, err: %d", errno);
		goto clean_up;
	}

	off = 0;
	do {
		bytes = send(fd, &request[off], len - off, 0);
		if (bytes < 0) {
			LOG_ERR("send() failed, err %d", errno);
			goto clean_up;
		}
		off += bytes;
	} while (off < len);

	LOG_INF("Sent %d bytes", off);

	bytes = recv(fd, recv_buf, RECV_BUF_SIZE, 0);
	if (bytes < 0) {
		LOG_ERR("recv() failed, err %d", errno);
		goto clean_up;
	}

	LOG_INF("Received %d bytes", bytes);

	/* Print HTTP response */
	LOG_INF("HTTP response: \n%s\n\n", recv_buf);

	LOG_INF("Finished, closing socket");

clean_up:
	freeaddrinfo(res);
	(void)close(fd);
}

static void print_cell_data(void)
{
	if (cell_data.current_cell.id == 0) {
		LOG_WRN("No cells were found");
		return;
	}

	printk("Current cell:\n");
	printk("\tMCC: %03d\n", cell_data.current_cell.mcc);
	printk("\tMNC: %03d\n", cell_data.current_cell.mnc);
	printk("\tCell ID: %d\n", cell_data.current_cell.id);
	printk("\tTAC: %d\n", cell_data.current_cell.tac);
	printk("\tEARFCN: %d\n", cell_data.current_cell.earfcn);
	printk("\tTiming advance: %d\n", cell_data.current_cell.timing_advance);
	printk("\tMeasurement time: %lld\n", cell_data.current_cell.measurement_time);
	printk("\tPhysical cell ID: %d\n", cell_data.current_cell.phys_cell_id);
	printk("\tRSRP: %d\n", cell_data.current_cell.rsrp);
	printk("\tRSRQ: %d\n", cell_data.current_cell.rsrq);

	if (cell_data.ncells_count == 0) {
		printk("*** No neighbor cells found ***\n");
		return;
	}

	for (size_t i = 0; i < cell_data.ncells_count; i++) {
		printk("Neighbor cell %d\n", i + 1);
		printk("\tEARFCN: %d\n", cell_data.neighbor_cells[i].earfcn);
		printk("\tTime difference: %d\n", cell_data.neighbor_cells[i].time_diff);
		printk("\tPhysical cell ID: %d\n", cell_data.neighbor_cells[i].phys_cell_id);
		printk("\tRSRP: %d\n", cell_data.neighbor_cells[i].rsrp);
		printk("\tRSRQ: %d\n", cell_data.neighbor_cells[i].rsrq);
	}
}

static void process_cell_data(void)
{
	int err;

	err = position_service_generete_request(&cell_data, http_request,
						sizeof(http_request));
	if (err) {
		LOG_ERR("Failed to generate HTTP request, error: %d", err);
		return;
	}

	send_request(http_request);
}

void main(void)
{
	int err;

	LOG_INF("Multicell positioning sample has started");

	k_work_init_delayable(&periodic_search_work, periodic_search_work_fn);

	err = cert_provision();
	if (err) {
		return;
	}

	lte_connect();

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("dk_buttons_init, error: %d", err);
	}

	LOG_INF("Connecting to LTE network, this may take several minutes...");

	k_sem_take(&lte_connected, K_FOREVER);

	LOG_INF("Connected to LTE network");

	if (IS_ENABLED(CONFIG_MULTICELL_PERIODIC_SEARCH)) {
		LOG_INF("Requesting neighbor cell information every %d seconds",
			CONFIG_MULTICELL_PERIODIC_SEARCH_INTERVAL);
		k_work_schedule(&periodic_search_work, K_NO_WAIT);
	}

	while (true) {
		k_sem_take(&cell_data_ready, K_FOREVER);
		if (CONFIG_MULTICELL_PRINT_DATA) {
			print_cell_data();
		}

		process_cell_data();
	}
}
