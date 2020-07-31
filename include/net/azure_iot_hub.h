/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief Azure IoT Hub library.
 */

#ifndef AZUE_IOT_HUB__
#define AZUE_IOT_HUB__

#include <stdio.h>
#include <net/mqtt.h>

/**
 * @defgroup azure_iot_hub Azure IoT Hub library
 * @{
 * @brief Library to connect the device to Azure IoT Hub.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**@ Broker disconnect results. */
enum azure_iot_hub_disconnect_reason {
	AZURE_IOT_HUB_DISCONNECT_USER_REQUEST,
	AZURE_IOT_HUB_DISCONNECT_CLOSED_BY_REMOTE,
	AZURE_IOT_HUB_DISCONNECT_INVALID_REQUEST,
	AZURE_IOT_HUB_DISCONNECT_MISC,
	AZURE_IOT_HUB_DISCONNECT_COUNT
};

/**@brief Broker connect results. */
enum azure_iot_hub_connect_result {
	AZURE_IOT_HUB_CONNECT_RES_SUCCESS = 0,
	AZURE_IOT_HUB_CONNECT_RES_ERR_NOT_INITD = -1,
	AZURE_IOT_HUB_CONNECT_RES_ERR_INVALID_PARAM = -2,
	AZURE_IOT_HUB_CONNECT_RES_ERR_NETWORK = -3,
	AZURE_IOT_HUB_CONNECT_RES_ERR_BACKEND = -4,
	AZURE_IOT_HUB_CONNECT_RES_ERR_MISC = -5,
	AZURE_IOT_HUB_CONNECT_RES_ERR_NO_MEM = -6,
	/* Invalid private key */
	AZURE_IOT_HUB_CONNECT_RES_ERR_PRV_KEY = -7,
	/* Invalid CA or client cert */
	AZURE_IOT_HUB_CONNECT_RES_ERR_CERT = -8,
	/* Other cert issue */
	AZURE_IOT_HUB_CONNECT_RES_ERR_CERT_MISC = -9,
	/* Timeout, SIM card may be out of data */
	AZURE_IOT_HUB_CONNECT_RES_ERR_TIMEOUT_NO_DATA = -10,
	AZURE_IOT_HUB_CONNECT_RES_ERR_ALREADY_CONNECTED = -11,
};

/** @brief Azure IoT Hub notification events used to notify the user. */
enum azure_iot_hub_evt_type {
	/** Connecting to Azure IoT Hub. */
	AZURE_IOT_HUB_EVT_CONNECTING = 0x1,
	/** Connected to Azure IoT Hub. */
	AZURE_IOT_HUB_EVT_CONNECTED,
	/** Azure IoT Hub established and ready. */
	AZURE_IOT_HUB_EVT_READY,
	/** Disconnected from Azure IoT Hub. */
	AZURE_IOT_HUB_EVT_DISCONNECTED,
	/** Device-bound data received from IoT Hub. */
	AZURE_IOT_HUB_EVT_DATA_RECEIVED,
	/** Device twin has been received. */
	AZURE_IOT_HUB_EVT_TWIN,
	/** Device twin has received a desired property update. */
	AZURE_IOT_HUB_EVT_TWIN_DESIRED,
	/** Device twin update successful. The request ID and status are contained
	 *  in the data.result member of the event structure.
	 */
	AZURE_IOT_HUB_EVT_TWIN_RESULT_SUCCESS,
	/** Device twin update failed. The request ID and status are contained
	 *  in the data.result member of the event structure.
	 */
	AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL,
	/** Device Provisioning Service has started. */
	AZURE_IOT_HUB_EVT_DPS_STARTED,
	/** DPS is done, and IoT hub information obtained. */
	AZURE_IOT_HUB_EVT_DPS_DONE,
	/** DPS failed to retrieve information to connect to IoT Hub. */
	AZURE_IOT_HUB_EVT_DPS_FAILED,
	/** Direct method invoked from the cloud side.
	 *  @note After a direct method has been executed,
	 *  @a azure_iot_hub_method_respond must be called to report back
	 *  the result of the method invocation.
	 */
	AZURE_IOT_HUB_EVT_DIRECT_METHOD,
	/** FOTA update start. */
	AZURE_IOT_HUB_EVT_FOTA_START,
	/** FOTA update done, reboot required to apply update. */
	AZURE_IOT_HUB_EVT_FOTA_DONE,
	/** FOTA erase pending. On nRF9160-based devices this is typically
	 *  caused by an active LTE connection preventing erase operation.
	 */
	AZURE_IOT_HUB_EVT_FOTA_ERASE_PENDING,
	/** FOTA erase done. */
	AZURE_IOT_HUB_EVT_FOTA_ERASE_DONE,
};

/** @brief Azure IoT Hub topic type, used to route messages to the correct
 *	   destination.
 */
enum aws_iot_topic_type {
	AZURE_IOT_HUB_TOPIC_DEVICEBOUND,
	AZURE_IOT_HUB_TOPIC_EVENT,
	AZURE_IOT_HUB_TOPIC_TWIN_DESIRED,
	AZURE_IOT_HUB_TOPIC_TWIN_REPORT,
	AZURE_IOT_HUB_TOPIC_TWIN_REQUEST,
};

/** @brief Azure IoT Hub topic data. */
struct azure_iot_hub_topic_data {
	/** Topic type. */
	enum aws_iot_topic_type type;
	/** Pointer to topic name. */
	char *str;
	/** Length of topic name. */
	size_t len;
};

/** @brief Azure IoT Hub transmission data. */
struct azure_iot_hub_data {
	/** Topic data is sent/received on. */
	struct azure_iot_hub_topic_data topic;
	/** Pointer to data sent/received from the Azure IoT Hub broker. */
	char *ptr;
	/** Length of data. */
	size_t len;
	/** Quality of Service of the message. */
	enum mqtt_qos qos;
};

/** @brief Azure ioT Hub direct method data. */
struct azure_iot_hub_method {
	/** Method name, null-terminated. */
	const char *name;
	/** Method request ID. */
	uint32_t rid;
	/** Method payload in JSON format. */
	const char *payload;
	/** Method payload length. */
	size_t payload_len;
};

/** @brief Azure ioT Hub result structure.
 *
 *  @details Used to signal result of direct
 *	     method execution from device to cloud, and to receive result of
 *	     device twin updates (twin updates sent from the device will
 *	     receive a result message back from the cloud with success or
 *	     failure).
 */
struct azure_iot_hub_result {
	/** Request ID to which the result belongs. */
	uint32_t rid;
	/** Status code. */
	uint32_t status;
	/** JSON payload. */
	char *payload;
	/** Size of payload. */
	size_t payload_len;
};

/** @brief Struct with data received from Azure IoT Hub broker. */
struct azure_iot_hub_evt {
	/** Type of event. */
	enum azure_iot_hub_evt_type type;
	union {
		struct azure_iot_hub_data msg;
		struct azure_iot_hub_method method;
		struct azure_iot_hub_result result;
		int err;
		bool persistent_session;
	} data;
	struct azure_iot_hub_topic_data topic;
};

/** @brief Azure IoT Hub library event handler.
 *
 *  @param evt Pointer to event.
 */
typedef void (*azure_iot_hub_evt_handler_t)(struct azure_iot_hub_evt *evt);

/** @brief Structure for Azure IoT Hub broker connection parameters. */
struct azure_iot_hub_config {
	/** Socket for Azure IoT Hub broker connection */
	int socket;
	/** Device id for Azure IoT Hub broker connection.
	 *  If NULL, the device ID provided by Kconfig is used.
	 */
	char *device_id;
	/** Length of device string. */
	size_t device_id_len;
};

/** @brief Initialize the module.
 *
 *  @warning This API must be called exactly once, and it must return
 *           successfully.
 *
 *  @param[in] config Pointer to struct containing connection parameters.
 *  @param[in] event_handler Pointer to event handler to receive Azure IoT Hub module
 *                           events.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_init(const struct azure_iot_hub_config *const config,
		 azure_iot_hub_evt_handler_t event_handler);

/** @brief Connect to the Azure IoT Hub broker.
 *
 *  @details On success, This function exposes the MQTT socket to the calling
 *	     function. If CONFIG_AZURE_IOT_HUB_CONNECTION_POLL_THREAD is not
 *	     enabled, the library expects the calling function to use some
 *	     mechanism, for example poll(), to detect when there is incoming
 *	     data on the socket. In that event, @a azure_iot_hub_input() must
 *	     be called to receive and process the data.
 *	     Alternatively, the function can be periodically called, though
 *	     this is less efficient.
 *	     Data received on the socket must be processed in order to complete
 *	     the connection procedure with the IoT hub.
 *
 *  @return When successful, a non-negative number indicating socket number is
 * 	    returned. Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_connect(void);

/** @brief Disconnect from the Azure IoT Hub broker.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_disconnect(void);

/** @brief Send data to Azure IoT Hub broker.
 *
 *  @param[in] tx_data Pointer to struct containing data to be transmitted to
 *                     the Azure IoT Hub broker.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_send(const struct azure_iot_hub_data *const tx_data);

/** @brief Get data from Azure IoT Hub broker.
 *
 *  @details Implicitly this function calls a non-blocking recv() on the
 *	     socket with IoT hub connection. It can be polled periodically,
 *	     but optimally only when there is actually data to be received.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_input(void);

/** @brief Ping Azure IoT Hub broker. Must be called periodically
 *         to keep connection to broker alive.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_ping(void);

/** @brief Send response to a direct method invoked from cloud side.
 *
 *  @param[in] result Structure containing result data from the direct method
 *		      execution.
 *
 *  @return 0 If successful.
 *          Otherwise, a (negative) error code is returned.
 */
int azure_iot_hub_method_respond(struct azure_iot_hub_result *result);

/** @brief Set device twin property.
 *
 *  @param[in] topic_list Pointer to list of topics.
 *  @param[in] list_count Number of entries in the list.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
// int azure_iot_hub_twin_set(char *name, )

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* AZUE_IOT_HUB__ */
