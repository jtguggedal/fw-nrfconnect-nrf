/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _LOCATION_MSG_H_
#define _LOCATION_MSG_H_

/**
 * @brief Location module messages
 * @defgroup location_module_msg Location module messages
 * @{
 */

#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>
#if defined(CONFIG_NRF_CLOUD_PGPS)
#include <net/nrf_cloud_pgps.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LOCATION_MODULE_MSG_TYPES			\
	X(LOCATION_MSG_GNSS_DATA_READY)			\
	X(LOCATION_MSG_DATA_NOT_READY)			\
	X(LOCATION_MSG_NEIGHBOR_CELLS_DATA_READY)	\
	X(LOCATION_MSG_TIMEOUT)				\
	X(LOCATION_MSG_ACTIVE)				\
	X(LOCATION_MSG_INACTIVE)			\
	X(LOCATION_MSG_SHUTDOWN_READY)			\
	X(LOCATION_MSG_AGPS_NEEDED)			\
	X(LOCATION_MSG_PGPS_NEEDED)			\
	X(LOCATION_MSG_ERROR_CODE)

/** @brief Position, velocity and time (PVT) data. */
struct location_module_pvt {
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

/** @brief Location module data for associated payload for messages of
 *         LOCATION_MODULE_MSG_NEIGHBOR_CELLS_DATA_READY types.
 */
struct location_module_neighbor_cells {
	/** Information about the current cell. */
	struct lte_lc_cells_info cell_data;
	/** Information about the neighbor cells. */
	struct lte_lc_ncell neighbor_cells[17];
	/** Uptime when the message was sent. */
	int64_t timestamp;
};

/** @brief Location module data for associated payload for messages of LOCATION_MODULE_MSG_TIMEOUT
 * 	   and LOCATION_MODULE_MSG_GNSS_DATA_READY types.
 */
struct location_module_data {
	/** Location data in the form of a PVT structure. */
	struct location_module_pvt pvt;

	/** Number of satellites tracked. */
	uint8_t satellites_tracked;

	/** Time when the search was initiated until fix or timeout occurred. */
	uint32_t search_time;

	/** Uptime when location was sampled. */
	int64_t timestamp;
};

/** @brief Location module message. */
struct location_module_msg {
	union {
		/** Data for message LOCATION_MODULE_MSG_GNSS_DATA_READY. */
		struct location_module_data location;
		/** Data for message LOCATION_MODULE_MSG_NEIGHBOR_CELLS_DATA_READY. */
		struct location_module_neighbor_cells neighbor_cells;
		/** Data for message LOCATION_MODULE_MSG_AGPS_NEEDED. */
		struct nrf_modem_gnss_agps_data_frame agps_request;
#if defined(CONFIG_NRF_CLOUD_PGPS)
		/** Data for message LOCATION_MODULE_MSG_PGPS_NEEDED. */
		struct gps_pgps_request pgps_request;
#endif
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Cause of the error for message LOCATION_MODULE_MSG_ERROR_CODE. */
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
