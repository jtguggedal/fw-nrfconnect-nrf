/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief AWS IoT library header.
 */

#ifndef UDP_BACKEND_H__
#define UDP_BACKEND_H__

#include <stdio.h>

/**
 * @defgroup udp_backend UDP backend library
 * @{
 * @brief Library to connect the device to a UDP server.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AWS IoT notification event types, used to signal the application. */
enum udp_backend_evt_type {
	/** Connected to UDP server. */
	UDP_BACKEND_EVT_CONNECTED = 0x1,
	/** UDP server ready. */
	UDP_BACKEND_EVT_READY,
	/** Disconnected to UDP server. */
	UDP_BACKEND_EVT_DISCONNECTED,
	/** Data received from AWS message broker. */
	UDP_BACKEND_EVT_DATA_RECEIVED,
	/** FOTA update done, request to reboot. */
	UDP_BACKEND_EVT_FOTA_DONE
};

/** @brief Struct with data received from UDP server. */
struct udp_backend_evt {
	/** Type of event. */
	enum udp_backend_evt_type type;
	/** Pointer to data received from the UDP server. */
	char *ptr;
	/** Length of data. */
	size_t len;
};

/** @brief UDP backend transmission data. */
struct udp_backend_tx_data {
	/** Pointer to message to be sent to UDP server. */
	char *str;
	/** Length of message. */
	size_t len;
};

/** @brief AWS IoT library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*udp_backend_evt_handler_t)(const struct udp_backend_evt *evt);

/** @brief Structure for UDP server connection parameters. */
struct udp_backend_config {
	/** Socket for UDP server connection */
	int socket;
};

/** @brief Initialize the module.
 *
 *  @warning This API must be called exactly once, and it must return
 *           successfully.
 *
 *  @param[in] config Pointer to struct containing connection parameters.
 *  @param[in] event_handler Pointer to event handler to receive AWS IoT module
 *                           events.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_init(const struct udp_backend_config *const config,
		 udp_backend_evt_handler_t event_handler);

/** @brief Connect to the UDP server.
 *
 *  @details This function exposes the UDP socket to main so that it can be
 *           polled on.
 *
 *  @param[out] config Pointer to struct containing connection parameters,
 *                     the UDP connection socket number will be copied to the
 *                     socket entry of the struct.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_connect(struct udp_backend_config *const config);

/** @brief Disconnect from the UDP server.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_disconnect(void);

/** @brief Send data to UDP server.
 *
 *  @param[in] tx_data Pointer to struct containing data to be transmitted to
 *                     the UDP server.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_send(const struct udp_backend_tx_data *const tx_data);

/** @brief Get data from UDP server
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_input(void);

/** @brief Ping UDP server. Must be called periodically
 *         to keep socket open.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int udp_backend_ping(void);


#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* UDP_BACKEND_H__ */
