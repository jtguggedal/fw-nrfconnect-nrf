/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <nrf_modem.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <modem/pdn.h>

#define MODULE modem_module

#include "module_common.h"

#ifdef CONFIG_LWM2M_CARRIER
#include <lwm2m_carrier.h>
#endif /* CONFIG_LWM2M_CARRIER */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_MODEM_MODULE_LOG_LEVEL);

BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT),
		"The Modem module does not support this configuration");

/* Modem module super states. */
static enum state_type {
	/* Initialization state where all libraries that the module depends
	 * on need to be initialized before you can enter any other state.
	 */
	STATE_INIT,
	STATE_DISCONNECTED,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_SHUTDOWN,
} state;

/* Enumerator that specifies the data type that is sampled. */
enum sample_type {
	MODEM_DYNAMIC,
	MODEM_STATIC,
	BATTERY_VOLTAGE
};

/* Struct that holds data from the modem information module. */
static struct modem_param_info modem_param;

/* Value that holds the latest RSRP value. */
static int16_t rsrp_value_latest;

/* Value that holds the latest LTE network mode. */
static enum lte_lc_lte_mode nw_mode_latest;

const k_tid_t module_thread;

/* Modem module message queue. */
#define MODEM_QUEUE_ENTRY_COUNT		10
#define MODEM_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(modem_module_msgq, sizeof(struct module_msg),
	      MODEM_QUEUE_ENTRY_COUNT, MODEM_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "modem",
	.msg_q = &modem_module_msgq,
	.supports_shutdown = true,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *modem_module = &self;

/* Forward declarations. */
static void send_cell_update(uint32_t cell_id, uint32_t tac);
static void send_psm_update(int tau, int active_time);
static void send_edrx_update(float edrx, float ptw);
static inline int adjust_rsrp(int input);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_DISCONNECTED:
		return "STATE_DISCONNECTED";
	case STATE_CONNECTING:
		return "STATE_CONNECTING";
	case STATE_CONNECTED:
		return "STATE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown state";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

/* Handlers */

void nrf_modem_fault_handler(struct nrf_modem_fault_info *fault_info)
{
	LOG_ERR("Modem error: 0x%x, PC: 0x%x", fault_info->reason, fault_info->program_counter);
	SEND_ERROR(MODEM_MSG_ERROR, -EFAULT);
}

static void lte_evt_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS: {
		if (evt->nw_reg_status == LTE_LC_NW_REG_UICC_FAIL) {
			LOG_ERR("No SIM card detected!");
			SEND_ERROR(MODEM_MSG_ERROR, -ENOTSUP);
			break;
		}

		if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) ||
		    (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			LOG_DBG("Network registration status: %s",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
				"Connected - home network" : "Connected - roaming");
		}

		break;
	}
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM parameter update: TAU: %d, Active time: %d",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		send_psm_update(evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %.2f, PTW: %.2f",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			LOG_DBG("%s", log_buf);
		}

		send_edrx_update(evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
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
		send_cell_update(evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		nw_mode_latest = evt->lte_mode;
		break;
	case LTE_LC_EVT_MODEM_EVENT:
		LOG_DBG("Modem domain event, type: %s",
			evt->modem_evt == LTE_LC_MODEM_EVT_LIGHT_SEARCH_DONE ?
				"Light search done" :
			evt->modem_evt == LTE_LC_MODEM_EVT_SEARCH_DONE ?
				"Search done" :
			evt->modem_evt == LTE_LC_MODEM_EVT_RESET_LOOP ?
				"Reset loop" :
			evt->modem_evt == LTE_LC_MODEM_EVT_BATTERY_LOW ?
				"Low battery" :
			evt->modem_evt == LTE_LC_MODEM_EVT_OVERHEATED ?
				"Modem is overheated" :
				"Unknown");

		/* If a reset loop happens in the field, it should not be necessary
		 * to perform any action. The modem will try to re-attach to the LTE network after
		 * the 30-minute block.
		 */
		if (evt->modem_evt == LTE_LC_MODEM_EVT_RESET_LOOP) {
			LOG_WRN("The modem has detected a reset loop. LTE network attach is now "
				"restricted for the next 30 minutes. Power-cycle the device to "
				"circumvent this restriction. For more information see the "
				"nRF91 AT Commands - Command Reference Guide v2.0 - chpt. 5.36");
		}
		break;
	default:
		break;
	}
}

/* Handler that notifies the application of events related to the default PDN context, CID 0. */
void pdn_event_handler(uint8_t cid, enum pdn_event event, int reason)
{
	ARG_UNUSED(cid);

	switch (event) {
	case PDN_EVENT_CNEC_ESM:
		LOG_WRN("Event: PDP context %d, %s", cid, pdn_esm_strerror(reason));
		break;
	case PDN_EVENT_ACTIVATED:
		LOG_DBG("PDN_EVENT_ACTIVATED");
		(void)SEND_MSG_ALL(MODEM_MSG_LTE_CONNECTED);
		break;
	case PDN_EVENT_DEACTIVATED:
		LOG_DBG("PDN_EVENT_DEACTIVATED");

		(void)SEND_MSG(&self, MODEM_MSG_LTE_DISCONNECTED);
		(void)SEND_MSG(cloud_module, MODEM_MSG_LTE_DISCONNECTED);
		break;
	case PDN_EVENT_IPV6_UP:
		LOG_DBG("PDN_EVENT_IPV6_UP");
		break;
	case PDN_EVENT_IPV6_DOWN:
		LOG_DBG("PDN_EVENT_IPV6_DOWN");
		break;
	default:
		LOG_WRN("Unexpected PDN event!");
		break;
	}
}

static void modem_rsrp_handler(char rsrp_value)
{
	/* RSRP raw values that represent actual signal strength are
	 * 0 through 97. RSRP is converted to dBm per "nRF91 AT Commands" v1.7.
	 */

	if (rsrp_value > 97) {
		return;
	}

	/* Set temporary variable to hold RSRP value. RSRP callbacks and other
	 * data from the modem info module are retrieved separately.
	 * This temporarily saves the latest value which are sent to
	 * the Data module upon a modem data request.
	 */
	rsrp_value_latest = adjust_rsrp(rsrp_value);

	LOG_DBG("Incoming RSRP status message, RSRP value is %d",
		rsrp_value_latest);
}

#ifdef CONFIG_LWM2M_CARRIER
static void print_carrier_error(const lwm2m_carrier_event_t *evt)
{
	const lwm2m_carrier_event_error_t *err = evt->data.error;
	static const char *const strerr[] = {
		[LWM2M_CARRIER_ERROR_NO_ERROR] =
			"No error",
		[LWM2M_CARRIER_ERROR_BOOTSTRAP] =
			"Bootstrap error",
		[LWM2M_CARRIER_ERROR_LTE_LINK_UP_FAIL] =
			"Failed to connect to the LTE network",
		[LWM2M_CARRIER_ERROR_LTE_LINK_DOWN_FAIL] =
			"Failed to disconnect from the LTE network",
		[LWM2M_CARRIER_ERROR_FOTA_PKG] =
			"Package refused from modem",
		[LWM2M_CARRIER_ERROR_FOTA_PROTO] =
			"Protocol error",
		[LWM2M_CARRIER_ERROR_FOTA_CONN] =
			"Connection to remote server failed",
		[LWM2M_CARRIER_ERROR_FOTA_CONN_LOST] =
			"Connection to remote server lost",
		[LWM2M_CARRIER_ERROR_FOTA_FAIL] =
			"Modem firmware update failed",
		[LWM2M_CARRIER_ERROR_CONFIGURATION] =
			"Illegal object configuration detected",
		[LWM2M_CARRIER_ERROR_INIT] =
			"Initialization failure",
		[LWM2M_CARRIER_ERROR_INTERNAL] =
			"Internal failure",
	};

	__ASSERT(PART_OF_ARRAY(strerr, &strerr[err->type]), "Unhandled carrier library error");

	LOG_ERR("%s, reason %d\n", strerr[err->type], err->value);
}

static void print_carrier_deferred_reason(const lwm2m_carrier_event_t *evt)
{
	const lwm2m_carrier_event_deferred_t *def = evt->data.deferred;
	static const char *const strdef[] = {
		[LWM2M_CARRIER_DEFERRED_NO_REASON] =
			"No reason given",
		[LWM2M_CARRIER_DEFERRED_PDN_ACTIVATE] =
			"Failed to activate PDN",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_NO_ROUTE] =
			"No route to bootstrap server",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_CONNECT] =
			"Failed to connect to bootstrap server",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_SEQUENCE] =
			"Bootstrap sequence not completed",
		[LWM2M_CARRIER_DEFERRED_SERVER_NO_ROUTE] =
			"No route to server",
		[LWM2M_CARRIER_DEFERRED_SERVER_CONNECT] =
			"Failed to connect to server",
		[LWM2M_CARRIER_DEFERRED_SERVER_REGISTRATION] =
			"Server registration sequence not completed",
		[LWM2M_CARRIER_DEFERRED_SERVICE_UNAVAILABLE] =
			"Server in maintenance mode",
		[LWM2M_CARRIER_DEFERRED_SIM_MSISDN] =
			"Waiting for SIM MSISDN",
	};

	__ASSERT(PART_OF_ARRAY(strdef, &strdef[def->reason]),
		"Unhandled deferred carrier library error");

	LOG_ERR("Reason: %s, timeout: %d seconds\n", strdef[def->reason], def->timeout);
}

int lwm2m_carrier_event_handler(const lwm2m_carrier_event_t *evt)
{
	int err = 0;

	switch (evt->type) {
	case LWM2M_CARRIER_EVENT_INIT: {
		LOG_INF("LWM2M_CARRIER_EVENT_INIT");
		SEND_MSG(&self, MODEM_MSG_CARRIER_INITIALIZED);
		break;
	}
	case LWM2M_CARRIER_EVENT_LTE_LINK_UP: {
		LOG_INF("LWM2M_CARRIER_EVENT_LTE_LINK_UP");
		SEND_MSG(&self, MODEM_MSG_CARRIER_EVENT_LTE_LINK_UP_REQUEST);
		break;
	}
	case LWM2M_CARRIER_EVENT_LTE_LINK_DOWN: {
		LOG_INF("LWM2M_CARRIER_EVENT_LTE_LINK_DOWN");
		SEND_MSG(&self, MODEM_MSG_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST);
		break;
	}
	case LWM2M_CARRIER_EVENT_LTE_POWER_OFF:
		LOG_INF("LWM2M_CARRIER_EVENT_LTE_POWER_OFF");
		break;
	case LWM2M_CARRIER_EVENT_BOOTSTRAPPED:
		LOG_INF("LWM2M_CARRIER_EVENT_BOOTSTRAPPED");
		break;
	case LWM2M_CARRIER_EVENT_REGISTERED:
		LOG_INF("LWM2M_CARRIER_EVENT_REGISTERED");
		break;
	case LWM2M_CARRIER_EVENT_DEFERRED:
		LOG_INF("LWM2M_CARRIER_EVENT_DEFERRED");
		print_carrier_deferred_reason(evt);
		break;
	case LWM2M_CARRIER_EVENT_FOTA_START: {
		LOG_INF("LWM2M_CARRIER_EVENT_FOTA_START");
		SEND_MSG(cloud_module MODEM_MSG_CARRIER_FOTA_PENDING);
		break;
	}
	case LWM2M_CARRIER_EVENT_REBOOT: {
		LOG_INF("LWM2M_CARRIER_EVENT_REBOOT");
		SEND_MSG(util_module MODEM_MSG_CARRIER_REBOOT_REQUEST);

		/* 1 is returned here to indicate to the carrier library that
		 * the application will handle rebooting of the system to
		 * ensure it happens gracefully. The alternative is to
		 * return 0 and let the library reboot at its convenience.
		 */
		return 1;
	}
	case LWM2M_CARRIER_EVENT_ERROR: {
		const lwm2m_carrier_event_error_t *err = evt->data.error;

		LOG_ERR("LWM2M_CARRIER_EVENT_ERROR");
		print_carrier_error(evt);

		bool fota_error = err->type == LWM2M_CARRIER_ERROR_FOTA_PKG ||
				  err->type == LWM2M_CARRIER_ERROR_FOTA_PROTO ||
				  err->type == LWM2M_CARRIER_ERROR_FOTA_CONN ||
				  err->type == LWM2M_CARRIER_ERROR_FOTA_CONN_LOST ||
				  err->type == LWM2M_CARRIER_ERROR_FOTA_FAIL;
		if (fota_error) {
			SEND_MSG(&self, MODEM_MSG_CARRIER_FOTA_STOPPED);
		}
		break;
	}
	}
	return err;
}
#endif /* CONFIG_LWM2M_CARRIER */


/* Static module functions. */
static void send_cell_update(uint32_t cell_id, uint32_t tac)
{
	// int err;
	// // TODO: Is this message reeally needed?
	// struct module_msg msg = {
	// 	.type = MODEM_MSG_LTE_CELL_UPDATE,
	// 	.modem.cell = {
	// 		.cell_id = cell_id,
	// 		.tac = tac,
	// 	},
	// };

	// err = module_send_msg_all(&msg);
	// if (err) {
	// 	LOG_ERR("Failed to send cell ID update, error: %d", err);
	// }
}

static void send_psm_update(int tau, int active_time)
{
	// int err;
	// // // TODO: Is this message reeally needed?
	// struct module_msg msg = {
	// 	.type = MODEM_MSG_LTE_PSM_UPDATE,
	// 	.modem.psm = {
	// 		.tau = tau,
	// 		.active_time = active_time,
	// 	},
	// };

	// err = module_send_msg_all(&msg);
	// if (err) {
	// 	LOG_ERR("Failed to send PSM update, error: %d", err);
	// }
}

static void send_edrx_update(float edrx, float ptw)
{
	// int err;
	// // // // TODO: Is this message reeally needed?
	// struct module_msg msg = {
	// 	.type = MODEM_MSG_LTE_EDRX_UPDATE,
	// 	.modem.edrx = {
	// 		.edrx = edrx,
	// 		.ptw = ptw,
	// 	},
	// };

	// err = module_send_msg_all(&msg);
	// if (err) {
	// 	LOG_ERR("Failed to send eDRX update, error: %d", err);
	// }
}

static inline int adjust_rsrp(int input)
{
	if (IS_ENABLED(CONFIG_MODEM_DYNAMIC_DATA_CONVERT_RSRP_TO_DBM)) {
		return RSRP_IDX_TO_DBM(input);
	}

	return input;
}

static int static_modem_data_get(void)
{
	int err;
	struct module_msg msg = {
		.type = MODEM_MSG_MODEM_STATIC_DATA_READY,
		.modem.modem_static.timestamp = k_uptime_get(),
	};

	/* Request data from modem information module. */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	strncpy(msg.modem.modem_static.app_version,
		CONFIG_ASSET_TRACKER_V2_APP_VERSION,
		sizeof(msg.modem.modem_static.app_version) - 1);

	strncpy(msg.modem.modem_static.board_version,
		modem_param.device.board,
		sizeof(msg.modem.modem_static.board_version) - 1);

	strncpy(msg.modem.modem_static.modem_fw,
		modem_param.device.modem_fw.value_string,
		sizeof(msg.modem.modem_static.modem_fw) - 1);

	strncpy(msg.modem.modem_static.iccid,
		modem_param.sim.iccid.value_string,
		sizeof(msg.modem.modem_static.iccid) - 1);

	strncpy(msg.modem.modem_static.imei,
		modem_param.device.imei.value_string,
		sizeof(msg.modem.modem_static.imei) - 1);

	msg.modem.modem_static.app_version[sizeof(msg.modem.modem_static.app_version) - 1] = '\0';
	msg.modem.modem_static.board_version
		[sizeof(msg.modem.modem_static.board_version) - 1] = '\0';
	msg.modem.modem_static.modem_fw[sizeof(msg.modem.modem_static.modem_fw) - 1] = '\0';
	msg.modem.modem_static.iccid[sizeof(msg.modem.modem_static.iccid) - 1] = '\0';
	msg.modem.modem_static.imei[sizeof(msg.modem.modem_static.imei) - 1] = '\0';

	err = module_send_msg(data_module, &msg);
	if (err) {
		LOG_ERR("Failed to send static modem data, error: %d", err);
	}

	err = module_send_msg(app_module, &msg);
	if (err) {
		LOG_ERR("Failed to send static modem data, error: %d", err);
	}

	return err;
}

static void populate_msg_with_dynamic_modem_data(struct module_msg *msg,
						 struct modem_param_info *param)
{
	/* If this flag is set all sampled parameter values will be included in the event regardless
	 * if they have changed or not.
	 */
	bool include = IS_ENABLED(CONFIG_MODEM_SEND_ALL_SAMPLED_DATA);

	/* Flag that checks if parameters has been added to the event. */
	bool params_added = false;

	/* Set all entries in the dynamic modem data structure to 0 to be sure that all 'fresh'
	 * flags become false by default. This is to avoid sending garbage or old data due to a flag
	 * being accidently set to true.
	 */
	memset(&msg->modem.modem_dynamic, 0, sizeof(struct modem_module_dynamic_modem_data));

	/* Structure that holds previous sampled dynamic modem data. By default, set all members of
	 * the structure to invalid values.
	 */
	static struct modem_module_dynamic_modem_data prev = {
		.rsrp = UINT8_MAX,
		.nw_mode = LTE_LC_LTE_MODE_NONE,
	};

	/* Compare the latest sampled parameters with the previous. If there has been a change we
	 * want to include the parameters in the event.
	 */
	if ((prev.rsrp != rsrp_value_latest) || include) {
		msg->modem.modem_dynamic.rsrp = rsrp_value_latest;
		prev.rsrp = rsrp_value_latest;

		msg->modem.modem_dynamic.rsrp_fresh = true;
		params_added = true;
	}

	if ((prev.band != param->network.current_band.value) || include) {
		msg->modem.modem_dynamic.band = param->network.current_band.value;
		prev.band = param->network.current_band.value;

		msg->modem.modem_dynamic.band_fresh = true;
		params_added = true;
	}

	if ((prev.nw_mode != nw_mode_latest) || include) {
		msg->modem.modem_dynamic.nw_mode = nw_mode_latest;
		prev.nw_mode = nw_mode_latest;

		msg->modem.modem_dynamic.nw_mode_fresh = true;
		params_added = true;
	}

	if ((strcmp(prev.apn, param->network.apn.value_string) != 0) || include) {
		strncpy(msg->modem.modem_dynamic.apn,
			modem_param.network.apn.value_string,
			sizeof(msg->modem.modem_dynamic.apn) - 1);

		strncpy(prev.apn,
			param->network.apn.value_string,
			sizeof(prev.apn) - 1);

		msg->modem.modem_dynamic.apn
			[sizeof(msg->modem.modem_dynamic.apn) - 1] = '\0';

		prev.apn[sizeof(prev.apn) - 1] = '\0';

		msg->modem.modem_dynamic.apn_fresh = true;
		params_added = true;
	}

	if ((strcmp(prev.ip_address, param->network.ip_address.value_string) != 0) || include) {
		strncpy(msg->modem.modem_dynamic.ip_address,
			modem_param.network.ip_address.value_string,
			sizeof(msg->modem.modem_dynamic.ip_address) - 1);

		strncpy(prev.ip_address,
			param->network.ip_address.value_string,
			sizeof(prev.ip_address) - 1);

		msg->modem.modem_dynamic.ip_address
			[sizeof(msg->modem.modem_dynamic.ip_address) - 1] = '\0';

		prev.ip_address[sizeof(prev.ip_address) - 1] = '\0';

		msg->modem.modem_dynamic.ip_address_fresh = true;
		params_added = true;
	}

	if ((prev.cell_id != param->network.cellid_dec) || include) {
		msg->modem.modem_dynamic.cell_id = param->network.cellid_dec;
		prev.cell_id = param->network.cellid_dec;

		msg->modem.modem_dynamic.cell_id_fresh = true;
		params_added = true;
	}

	if ((strcmp(prev.mccmnc, param->network.current_operator.value_string) != 0) || include) {
		strncpy(msg->modem.modem_dynamic.mccmnc,
			modem_param.network.current_operator.value_string,
			sizeof(msg->modem.modem_dynamic.mccmnc));

		strncpy(prev.mccmnc, param->network.current_operator.value_string,
			sizeof(prev.mccmnc));

		msg->modem.modem_dynamic.mccmnc
			[sizeof(msg->modem.modem_dynamic.mccmnc) - 1] = '\0';

		prev.mccmnc[sizeof(prev.mccmnc) - 1] = '\0';

		/* Provide MNC and MCC as separate values. */
		msg->modem.modem_dynamic.mcc = modem_param.network.mcc.value;
		msg->modem.modem_dynamic.mnc = modem_param.network.mnc.value;

		msg->modem.modem_dynamic.mccmnc_fresh = true;
		params_added = true;
	}

	if ((prev.area_code != modem_param.network.area_code.value) || include) {
		msg->modem.modem_dynamic.area_code = param->network.area_code.value;
		prev.area_code = param->network.area_code.value;

		msg->modem.modem_dynamic.area_code_fresh = true;
		params_added = true;
	}

	if (params_added) {
		msg->type = MODEM_MSG_MODEM_DYNAMIC_DATA_READY;
		msg->modem.modem_dynamic.timestamp = k_uptime_get();
	} else {
		LOG_DBG("No dynamic modem parameters have changed from the last sample request.");
		msg->type = MODEM_MSG_MODEM_DYNAMIC_DATA_NOT_READY;
	}
}

static int dynamic_modem_data_get(void)
{
	int err;
	struct module_msg msg = {
		.modem.modem_static.timestamp = k_uptime_get(),
	};

	/* Request data from modem information module. */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	populate_msg_with_dynamic_modem_data(&msg, &modem_param);

	err = module_send_msg(cloud_module, &msg);
	if (err) {
		LOG_ERR("Failed to send dynamic modem data, error: %d", err);
	}

	return err;
}

static bool data_type_is_requested(enum app_module_data_type *data_list,
				   size_t count,
				   enum app_module_data_type type)
{
	for (size_t i = 0; i < count; i++) {
		if (data_list[i] == type) {
			return true;
		}
	}

	return false;
}

static int battery_data_get(void)
{
	int err;
	struct module_msg msg = {
		.type = MODEM_MSG_BATTERY_DATA_READY,
		.modem.bat.timestamp = k_uptime_get(),
	};

	/* Replace this function with a function that specifically requests battery voltage. */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	msg.modem.bat.battery_voltage = modem_param.device.battery.value;

	err = module_send_msg(data_module, &msg);
	if (err) {
		LOG_ERR("Failed to send battery voltage, error: %d", err);
	}

	return err;
}

static int configure_low_power(void)
{
	int err;
	bool enable = IS_ENABLED(CONFIG_MODEM_AUTO_REQUEST_POWER_SAVING_FEATURES);

	err = lte_lc_psm_req(enable);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d", err);
		return err;
	}

	if (enable) {
		LOG_DBG("PSM requested");
	} else {
		LOG_DBG("PSM disabled");
	}

	return 0;
}

static int lte_connect(void)
{
	int err = lte_lc_connect_async(lte_evt_handler);

	if (err) {
		LOG_ERR("lte_lc_connect_async, error: %d", err);

		return err;
	}

	(void)SEND_MSG(&self, MODEM_MSG_LTE_CONNECTING);
	(void)SEND_MSG(ui_module, MODEM_MSG_LTE_CONNECTING);

	return 0;
}

static int modem_data_init(void)
{
	int err;

	err = modem_info_init();
	if (err) {
		LOG_INF("modem_info_init, error: %d", err);
		return err;
	}

	err = modem_info_params_init(&modem_param);
	if (err) {
		LOG_INF("modem_info_params_init, error: %d", err);
		return err;
	}

	err = modem_info_rsrp_register(modem_rsrp_handler);
	if (err) {
		LOG_INF("modem_info_rsrp_register, error: %d", err);
		return err;
	}

	return 0;
}

static int setup(void)
{
	int err;

	err = lte_lc_init();
	if (err) {
		LOG_ERR("lte_lc_init, error: %d", err);
		return err;
	}

	/* Setup a callback for the default PDP context. */
	err = pdn_default_ctx_cb_reg(pdn_event_handler);
	if (err) {
		LOG_ERR("pdn_default_ctx_cb_reg, error: %d", err);
		return err;
	}

	err = configure_low_power();
	if (err) {
		LOG_ERR("configure_low_power, error: %d", err);
		return err;
	}

	err = lte_lc_modem_events_enable();
	if (err) {
		LOG_ERR("lte_lc_modem_events_enable failed, error: %d", err);
		return err;
	}

	err = modem_data_init();
	if (err) {
		LOG_ERR("modem_data_init, error: %d", err);
		return err;
	}

	return 0;
}

/* Message handler for STATE_INIT */
static void on_state_init(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_CARRIER_INITIALIZED)) {
		int err;

		state_set(STATE_DISCONNECTED);

		err = setup();
		__ASSERT(err == 0, "Failed running setup()");

		SEND_MSG(cloud_module, MODEM_MSG_INITIALIZED);
		SEND_MSG(location_module, MODEM_MSG_INITIALIZED);

		err = lte_connect();
		if (err) {
			LOG_ERR("Failed connecting to LTE, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}
}

/* Message handler for STATE_DISCONNECTED. */
static void on_state_disconnected(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}

	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTING)) {
		state_set(STATE_CONNECTING);
	}

	if ((IS_MSG(msg, APP_MSG_LTE_DISCONNECT)) ||
	    (IS_MSG(msg, MODEM_MSG_CARRIER_EVENT_LTE_LINK_UP_REQUEST)) ||
	    (IS_MSG(msg, CLOUD_MSG_LTE_CONNECT))) {
		int err;

		err = lte_connect();
		if (err) {
			LOG_ERR("Failed connecting to LTE, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}
}

/* Message handler for STATE_CONNECTING. */
static void on_state_connecting(struct module_msg *msg)
{
	if ((IS_MSG(msg, APP_MSG_LTE_DISCONNECT)) ||
	    (IS_MSG(msg, CLOUD_MSG_LTE_DISCONNECT))) {
		int err;

		err = lte_lc_offline();
		if (err) {
			LOG_ERR("LTE disconnect failed, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
			return;
		}

		state_set(STATE_DISCONNECTED);
	}

	if (IS_MSG(msg, MODEM_MSG_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}
}

/* Message handler for STATE_CONNECTED. */
static void on_state_connected(struct module_msg *msg)
{
	if (IS_MSG(msg, MODEM_MSG_LTE_DISCONNECTED)) {
		state_set(STATE_DISCONNECTED);
	}

	if (IS_MSG(msg, MODEM_MSG_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST)) {
		int err;

		err = lte_lc_offline();
		if (err) {
			LOG_ERR("LTE disconnect failed, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}

	if ((IS_MSG(msg, APP_MSG_LTE_DISCONNECT)) ||
	    (IS_MSG(msg, MODEM_MSG_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST)) ||
	    (IS_MSG(msg, CLOUD_MSG_LTE_DISCONNECT))) {
		int err;

		err = lte_lc_offline();
		if (err) {
			LOG_ERR("LTE disconnect failed, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
			return;
		}

		state_set(STATE_DISCONNECTED);
	}
}

/* Message handler for all states. */
static void on_all_states(struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_USER_ASSOCIATION_REQUEST)) {
		int err = lte_lc_psm_req(false);

		if (err) {
			LOG_ERR("lte_lc_psm_req, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}

	if (IS_MSG(msg, CLOUD_MSG_USER_ASSOCIATED)) {

		/* Re-enable low power features after cloud has been associated. */
		int err = configure_low_power();

		if (err) {
			LOG_ERR("configure_low_power, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}

	if (IS_MSG(msg, APP_MSG_START)) {
		int err;

		if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
			/* The carrier library handles the LTE connection
			 * establishment.
			 */
			return;
		}

		err = lte_connect();
		if (err) {
			LOG_ERR("Failed connecting to LTE, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
			return;
		}
	}

	if (IS_MSG(msg, APP_MSG_DATA_GET)) {
		if (data_type_is_requested(msg->app.data_list,
					   msg->app.count,
					   APP_DATA_MODEM_STATIC)) {

			int err;

			err = static_modem_data_get();
			if (err) {
				SEND_MSG(data_module, MODEM_MSG_MODEM_STATIC_DATA_NOT_READY);
			}
		}

		if (data_type_is_requested(msg->app.data_list,
					   msg->app.count,
					   APP_DATA_MODEM_DYNAMIC)) {

			int err;

			err = dynamic_modem_data_get();
			if (err) {
				SEND_MSG(data_module, MODEM_MSG_MODEM_DYNAMIC_DATA_NOT_READY);
			}
		}

		if (data_type_is_requested(msg->app.data_list,
					   msg->app.count,
					   APP_DATA_BATTERY)) {

			int err;

			err = battery_data_get();
			if (err) {
				SEND_MSG(data_module, MODEM_MSG_BATTERY_DATA_NOT_READY);
			}
		}
	}

	if (IS_MSG(msg, UTIL_MSG_SHUTDOWN_REQUEST)) {
		lte_lc_power_off();
		state_set(STATE_SHUTDOWN);
		SEND_SHUTDOWN_ACK(MODEM_MSG_SHUTDOWN_READY, self.id);
	}
}

static void module_thread_fn(void)
{
	int err;
	struct module_msg msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(MODEM_MSG_ERROR, err);
	}

	if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
		state_set(STATE_INIT);
	} else {
		state_set(STATE_DISCONNECTED);

		SEND_MSG(cloud_module, MODEM_MSG_INITIALIZED);
		SEND_MSG(location_module, MODEM_MSG_INITIALIZED);

		err = setup();
		if (err) {
			LOG_ERR("Failed setting up the modem, error: %d", err);
			SEND_ERROR(MODEM_MSG_ERROR, err);
		}
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_DISCONNECTED:
			on_state_disconnected(&msg);
			break;
		case STATE_CONNECTING:
			on_state_connecting(&msg);
			break;
		case STATE_CONNECTED:
			on_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Invalid state: %d", state);
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(modem_module_thread, CONFIG_MODEM_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
