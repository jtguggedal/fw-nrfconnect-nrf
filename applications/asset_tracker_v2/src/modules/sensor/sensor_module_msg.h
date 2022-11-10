/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _SENSOR_MODULE_MSG_H_
#define _SENSOR_MODULE_MSG_H_

/**
 * @brief Sensor module messages
 * @defgroup sensor_module_message Sensor module messages
 * @{
 */

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_MODULE_MSG_TYPES				\
	X(SENSOR_MSG_MOVEMENT_ACTIVITY_DETECTED)	\
	X(SENSOR_MSG_MOVEMENT_INACTIVITY_DETECTED)	\
	X(SENSOR_MSG_MOVEMENT_IMPACT_DETECTED)		\
	X(SENSOR_MSG_ENVIRONMENTAL_DATA_READY)		\
	X(SENSOR_MSG_ENVIRONMENTAL_NOT_SUPPORTED)	\
	X(SENSOR_MSG_SHUTDOWN_READY)			\
	X(SENSOR_MSG_ERROR)

#define ACCELEROMETER_AXIS_COUNT 3

/** @brief Structure used to provide environmental data. */
struct sensor_module_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Temperature in Celsius degrees. */
	double temperature;
	/** Humidity in percentage. */
	double humidity;
	/** Atmospheric pressure in kilopascal. */
	double pressure;
	/** BSEC air quality in Indoor-Air-Quality (IAQ) index.
	 *  If -1, the value is not provided.
	 */
	int bsec_air_quality;
};

/** @brief Structure used to provide acceleration data. */
struct sensor_module_accel_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Acceleration in X, Y and Z planes in m/s2. */
	double values[ACCELEROMETER_AXIS_COUNT];
};

/** @brief Structure used to provide impact data. */
struct sensor_module_impact_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Acceleration on impact, measured in G. */
	double magnitude;
};

/** @brief Sensor module messages. */
struct sensor_module_msg {
	union {
		/** Variable that contains sensor readings. */
		struct sensor_module_data sensors;
		/** Variable that contains acceleration data. */
		struct sensor_module_accel_data accel;
		/** Variable that contains impact data. */
		struct sensor_module_impact_data impact;
		/** Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Code signifying the cause of error. */
		int err;
	};
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _SENSOR_MODULE_MSG_H_ */
