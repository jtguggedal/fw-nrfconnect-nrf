/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _LOCATION_MSG_H_
#define _LOCATION_MSG_H_

/**
 * @brief Location messages
 * @defgroup location_msg Location messages
 * @{
 */

#include <zephyr/zbus/zbus.h>
#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>
#if defined(CONFIG_NRF_CLOUD_PGPS)
#include <net/nrf_cloud_pgps.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct location_msg;

#define LOCATION_MSG_CHAN		location_msg_chan
#define LOCATION_MSG_PAYLOAD_TYPE	struct location_msg
#define LOCATION_MSG_NO_PAYLOAD		struct location_msg	/* Zbus docs says union or struct required */

#define LOCATION_MSG_TYPES								\
	X(LOCATION_MSG_GNSS_DATA_READY, 		LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_DATA_NOT_READY, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_NEIGHBOR_CELLS_DATA_READY, 	LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_TIMEOUT, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_ACTIVE, 				LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_INACTIVE, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_SHUTDOWN_READY, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_AGPS_NEEDED, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_PGPS_NEEDED, 			LOCATION_MSG_PAYLOAD_TYPE)	\
	X(LOCATION_MSG_ERROR_CODE, 			LOCATION_MSG_PAYLOAD_TYPE)

/** @brief Position, velocity and time (PVT) data. */
struct location_pvt {
	/** Longitude in degrees. */
	double longitude;

	/** Latitude in degrees. */
	double latitude;

	/** Altitude above WGS-84 ellipsoid in meters. */
	float altitude;

	/** Position accuracy (2D 1-sigma) in meters. */
	float accuracy;

	/** Horizontal speed in m/s. */
	float speed;

	/** Heading of user movement in degrees. */
	float heading;
};

/** @brief Location data for associated payload for messages of
 *         LOCATION_MSG_NEIGHBOR_CELLS_DATA_READY types.
 */
struct location_neighbor_cells {
	/** Information about the current cell. */
	struct lte_lc_cells_info cell_data;
	/** Information about the neighbor cells. */
	struct lte_lc_ncell neighbor_cells[17];
	/** Uptime when the message was sent. */
	int64_t timestamp;
};

/** @brief Location data for associated payload for messages of LOCATION_MSG_TIMEOUT
 * 	   and LOCATION_MSG_GNSS_DATA_READY types.
 */
struct location_info {
	/** Location data in the form of a PVT structure. */
	struct location_pvt pvt;

	/** Number of satellites tracked. */
	uint8_t satellites_tracked;

	/** Time when the search was initiated until fix or timeout occurred. */
	uint32_t search_time;

	/** Uptime when location was sampled. */
	int64_t timestamp;
};

/** @brief Location message. */
struct location_msg {
	union {
		/** Data for message LOCATION_MSG_GNSS_DATA_READY. */
		struct location_info location;
		/** Data for message LOCATION_MSG_NEIGHBOR_CELLS_DATA_READY. */
		struct location_neighbor_cells neighbor_cells;
		/** Data for message LOCATION_MSG_AGPS_NEEDED. */
		struct nrf_modem_gnss_agps_data_frame agps_request;
#if defined(CONFIG_NRF_CLOUD_PGPS)
		/** Data for message LOCATION_MSG_PGPS_NEEDED. */
		struct gps_pgps_request pgps_request;
#endif
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Cause of the error for message LOCATION_MSG_ERROR_CODE. */
		int err;
	};
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _LOCATION_MSG_H_ */
