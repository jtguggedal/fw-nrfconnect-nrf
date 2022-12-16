/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MODEM_MSG_H_
#define _MODEM_MSG_H_

/**
 * @brief Modem messages
 * @defgroup modem_msg Modem messages
 * @{
 */
#include <zephyr/net/net_ip.h>
#include <modem/lte_lc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEM_MSG_TYPES						\
	X(MODEM_MSG_INITIALIZED)				\
	X(MODEM_MSG_LTE_CONNECTED)				\
	X(MODEM_MSG_LTE_DISCONNECTED)				\
	X(MODEM_MSG_LTE_CONNECTING)				\
	X(MODEM_MSG_LTE_CELL_UPDATE)				\
	X(MODEM_MSG_LTE_PSM_UPDATE)				\
	X(MODEM_MSG_LTE_EDRX_UPDATE)				\
	X(MODEM_MSG_MODEM_STATIC_DATA_READY)			\
	X(MODEM_MSG_MODEM_STATIC_DATA_NOT_READY)		\
	X(MODEM_MSG_MODEM_DYNAMIC_DATA_READY)			\
	X(MODEM_MSG_MODEM_DYNAMIC_DATA_NOT_READY)		\
	X(MODEM_MSG_NEIGHBOR_CELLS_DATA_READY)			\
	X(MODEM_MSG_NEIGHBOR_CELLS_DATA_NOT_READY)		\
	X(MODEM_MSG_BATTERY_DATA_READY)				\
	X(MODEM_MSG_BATTERY_DATA_NOT_READY)			\
	X(MODEM_MSG_SHUTDOWN_READY)				\
	X(MODEM_MSG_CARRIER_INITIALIZED)			\
	X(MODEM_MSG_CARRIER_FOTA_PENDING)			\
	X(MODEM_MSG_CARRIER_FOTA_STOPPED)			\
	X(MODEM_MSG_CARRIER_REBOOT_REQUEST)			\
	X(MODEM_MSG_CARRIER_EVENT_LTE_LINK_UP_REQUEST)		\
	X(MODEM_MSG_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST)	\
	X(MODEM_MSG_ERROR)

/** @brief LTE cell information. */
struct modem_cell {
	/** E-UTRAN cell ID. */
	uint32_t cell_id;
	/** Tracking area code. */
	uint32_t tac;
};

/** @brief PSM information. */
struct modem_psm {
	/** Tracking Area Update interval [s]. -1 if the timer is disabled. */
	int tau;
	/** Active time [s]. -1 if the timer is disabled. */
	int active_time;
};

/** @brief eDRX information. */
struct modem_edrx {
	/** eDRX interval value [s] */
	float edrx;
	/** Paging time window [s] */
	float ptw;
};

struct modem_static_modem_data {
	int64_t timestamp;
	char iccid[23];
	char app_version[CONFIG_ASSET_TRACKER_V2_APP_VERSION_MAX_LEN];
	char board_version[30];
	char modem_fw[40];
	char imei[16];
};

struct modem_dynamic_modem_data {
	int64_t timestamp;
	uint16_t area_code;
	uint32_t cell_id;
	int16_t rsrp;
	uint16_t mcc;
	uint16_t mnc;
	char ip_address[INET6_ADDRSTRLEN];
	char apn[CONFIG_MODEM_APN_LEN_MAX];
	char mccmnc[7];
	uint8_t band;
	enum lte_lc_lte_mode nw_mode;

	/* Flags to signify if the corresponding data value has been updated and is concidered
	 * fresh.
	 */
	bool area_code_fresh	: 1;
	bool cell_id_fresh	: 1;
	bool rsrp_fresh		: 1;
	bool ip_address_fresh	: 1;
	bool mccmnc_fresh	: 1;
	bool band_fresh		: 1;
	bool nw_mode_fresh	: 1;
	bool apn_fresh		: 1;
};

struct modem_battery_data {
	uint16_t battery_voltage;
	int64_t timestamp;
};

struct modem_neighbor_cells {
	struct lte_lc_cells_info cell_data;
	struct lte_lc_ncell neighbor_cells[17];
	int64_t timestamp;
};

/** @brief Modem messages. */
struct modem_msg {
	union {
		struct modem_static_modem_data modem_static;
		struct modem_dynamic_modem_data modem_dynamic;
		struct modem_battery_data bat;
		struct modem_cell cell;
		struct modem_psm psm;
		struct modem_edrx edrx;
		struct modem_neighbor_cells neighbor_cells;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		int err;
	};
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _MODEM_MSG_H_ */
