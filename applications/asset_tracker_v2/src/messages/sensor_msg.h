/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _SENSOR_MSG_H_
#define _SENSOR_MSG_H_

/**
 * @brief Sensor messages
 * @defgroup sensor_message Sensor messages
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACCELEROMETER_AXIS_COUNT 3

/* Forward declaration of data type used in messages sent over Zbus channel. */
struct sensor_msg;

#define SENSOR_MSG_CHAN			sensor_msg_chan
#define SENSOR_MSG_PAYLOAD_TYPE		struct sensor_msg
#define SENSOR_MSG_NO_PAYLOAD		struct sensor_msg	/* Zbus docs says union or struct required */

#define SENSOR_MSG_TYPES								\
	X(SENSOR_MSG_MOVEMENT_ACTIVITY_DETECTED, 	SENSOR_MSG_NO_PAYLOAD)		\
	X(SENSOR_MSG_MOVEMENT_INACTIVITY_DETECTED, 	SENSOR_MSG_NO_PAYLOAD)		\
	X(SENSOR_MSG_MOVEMENT_IMPACT_DETECTED, 		SENSOR_MSG_NO_PAYLOAD)		\
	X(SENSOR_MSG_ENVIRONMENTAL_DATA_READY, 		SENSOR_MSG_PAYLOAD_TYPE)	\
	X(SENSOR_MSG_ENVIRONMENTAL_NOT_SUPPORTED, 	SENSOR_MSG_NO_PAYLOAD)		\
	X(SENSOR_MSG_SHUTDOWN_READY, 			SENSOR_MSG_PAYLOAD_TYPE)	\
	X(SENSOR_MSG_ERROR, 				SENSOR_MSG_PAYLOAD_TYPE)

/** @brief Structure used to provide environmental data. */
struct sensor_data {
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
struct sensor_accel_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Acceleration in X, Y and Z planes in m/s2. */
	double values[ACCELEROMETER_AXIS_COUNT];
};

/** @brief Structure used to provide impact data. */
struct sensor_impact_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Acceleration on impact, measured in G. */
	double magnitude;
};

/** @brief Sensor messages. */
struct sensor_msg {
	union {
		/** Variable that contains sensor readings. */
		struct sensor_data sensors;
		/** Variable that contains acceleration data. */
		struct sensor_accel_data accel;
		/** Variable that contains impact data. */
		struct sensor_impact_data impact;
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

#endif /* _SENSOR_MSG_H_ */
