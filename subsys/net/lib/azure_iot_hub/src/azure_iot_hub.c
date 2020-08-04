/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <net/mqtt.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <stdio.h>
#include <net/azure_iot_hub.h>
#include <settings/settings.h>

#include "azure_iot_hub_dps.h"

#if defined(CONFIG_AZURE_FOTA)
#include <net/azure_fota.h>
#endif

#if defined(CONFIG_BOARD_QEMU_X86)
#include "certificates.h"
#endif

#include <logging/log.h>

LOG_MODULE_REGISTER(azure_iot_hub, CONFIG_AZURE_IOT_HUB_LOG_LEVEL);
#define AZURE_IOT_HUB_AF_FAMILY AF_INET

#define USER_NAME_STATIC 	CONFIG_AZURE_IOT_HUB_HOSTNAME "/"	\
				CONFIG_AZURE_IOT_HUB_DEVICE_ID		\
				"/?api-version=2018-06-30"
/* User name when connecting to Azure IoT hub is on the form
 * 	<IoT Hub hostname>/<device ID>/?api-version=2018-06-30
 */
#define USER_NAME_TEMPLATE	"%s/%s/?api-version=2018-06-30"

#define TOPIC_DEVICEBOUND 	"devices/%s/messages/devicebound/#"
#define TOPIC_TWIN_DESIRED	"$iothub/twin/PATCH/properties/desired/#"
#define TOPIC_TWIN_RES		"$iothub/twin/res/#"
#define TOPIC_TWIN_REPORT	"$iothub/twin/PATCH/properties/reported/?$rid=%d"
#define TOPIC_EVENTS	 	"devices/%s/messages/events/"
#define TOPIC_TWIN_REQUEST	"$iothub/twin/GET/?$rid=%d"
#define TOPIC_DIRECT_METHODS	"$iothub/methods/POST/#"
#define TOPIC_DIRECT_METHOD_RES	"$iothub/methods/res/%d/?$rid=%d"

#define DPS_USER_NAME		CONFIG_AZURE_IOT_HUB_DPS_ID_SCOPE \
				"/registrations/%s/api-version=2019-03-31"

#define FOTA_PROGRESS_REPORT	"{\"firmware\":{\"status\":\"%s\",\"dl\":%d}}"
#define FOTA_MIN_REPORT_PROGRESS_STEP	20

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DEVICE_ID_APP)
static char device_id_buf[CONFIG_AZURE_IOT_HUB_DEVICE_ID_MAX_LEN];
#else
static char device_id_buf[] = CONFIG_AZURE_IOT_HUB_DEVICE_ID;
#endif

static struct azure_iot_hub_config conn_config = {
	.device_id = device_id_buf,
#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DEVICE_ID_APP)
	.device_id_len = 0,
#else
	.device_id_len = sizeof(CONFIG_AZURE_IOT_HUB_DEVICE_ID) - 1,
#endif
};

static azure_iot_hub_evt_handler_t evt_handler;

/* If DPS is used, the IoT hub name is obtained through that service, otherwise
 * it has to be set compile time using CONFIG_AZURE_IOT_HUB_NAME.
 * The maximal size is length of hub name + device ID length + lengths of
 * ".azure-devices-provisioning.net/" and "/?api-version=2018-06-30" */
#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
#define USER_NAME_BUF_LEN 	CONFIG_AZURE_IOT_HUB_NAME_MAX_LEN + \
				CONFIG_AZURE_IOT_HUB_DEVICE_ID_MAX_LEN + \
				sizeof(".azure-devices-provisioning.net/") + \
				sizeof("/?api-version=2018-06-30")
static char user_name_buf[USER_NAME_BUF_LEN];
static struct mqtt_utf8 user_name = {
	.utf8 = user_name_buf,
};
#else
static struct mqtt_utf8 user_name = {
	.utf8 = USER_NAME_STATIC,
	.size = sizeof(USER_NAME_STATIC) - 1,
};
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

static char rx_buffer[CONFIG_AZURE_IOT_HUB_MQTT_RX_TX_BUFFER_LEN];
static char tx_buffer[CONFIG_AZURE_IOT_HUB_MQTT_RX_TX_BUFFER_LEN];
static char payload_buf[CONFIG_AZURE_IOT_HUB_MQTT_PAYLOAD_BUFFER_LEN];

static bool is_initialized;
static struct mqtt_client client;
static struct sockaddr_storage broker;
static K_SEM_DEFINE(connection_poll_sem, 0, 1);
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(disconnected, 0, 1);
static atomic_t dps_disconnecting;

#if defined(CONFIG_CLOUD_API)
static struct cloud_backend *azure_iot_hub_backend;
#endif

/* Build time asserts */

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
/* Device ID source must be one of three for the library to work:
 *	- Kconfigured IoT hub device ID
 *	- Runtime app-provided device ID
 *	- Kconfigured as DPS registration ID
 */
BUILD_ASSERT((sizeof(CONFIG_AZURE_IOT_HUB_DEVICE_ID) - 1 > 0) ||
	     IS_ENABLED(CONFIG_AZURE_IOT_HUB_DEVICE_ID_APP) ||
	     (IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) &&
	     (sizeof(CONFIG_AZURE_IOT_HUB_DPS_REG_ID) - 1 > 0)),
	     "Device ID must be set by Kconfig or application");
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

/* Static function signatures */
static int connect_client(struct azure_iot_hub_config *cfg);

static void azure_iot_hub_notify_event(struct azure_iot_hub_evt *evt)
{
	evt_handler(evt);
}

static int publish_get_payload(struct mqtt_client *const c, size_t length)
{
	if (length > sizeof(payload_buf)) {
		LOG_ERR("Incoming MQTT message too large for payload buffer");
		return -EMSGSIZE;
	}

	return mqtt_readall_publish_payload(c, payload_buf, length);
}

static int topic_subscribe(void)
{
	int err;
	ssize_t len;
	char topic_devicebound[(sizeof(TOPIC_DEVICEBOUND) - 2) +
		CONFIG_AZURE_IOT_HUB_DEVICE_ID_MAX_LEN];
	struct mqtt_topic sub_topics[] = {
		{
			.topic.utf8 = topic_devicebound,
		},
		{
			.topic.size = sizeof(TOPIC_TWIN_DESIRED) - 1,
			.topic.utf8 = TOPIC_TWIN_DESIRED,
		},
		{
			.topic.size = sizeof(TOPIC_TWIN_RES) - 1,
			.topic.utf8 = TOPIC_TWIN_RES,
		},
		{
			.topic.size = sizeof(TOPIC_DIRECT_METHODS) - 1,
			.topic.utf8 = TOPIC_DIRECT_METHODS,
		},
	};
	const struct mqtt_subscription_list sub_list = {
		.list = sub_topics,
		.list_count = ARRAY_SIZE(sub_topics),
		.message_id = k_uptime_get_32(),
	};

	len = snprintk(topic_devicebound, sizeof(topic_devicebound),
		       TOPIC_DEVICEBOUND, device_id_buf);
	if ((len < 0) || (len > sizeof(topic_devicebound))) {
		LOG_ERR("Failed to create device bound topic");
		return -ENOMEM;
	}

	sub_topics[0].topic.size = len;

	for (size_t i = 0; i < sub_list.list_count; i++) {
		LOG_DBG("Subscribing to: %s",
			log_strdup(sub_list.list[i].topic.utf8));
	}

	err = mqtt_subscribe(&client, &sub_list);
	if (err) {
		LOG_ERR("Failed to subscribe to topic list, error: %d", err);
	}

	LOG_DBG("Successfully subscribed to default topics");

	return err;
}

static bool is_direct_method(const char *topic, size_t topic_len)
{
	/* Compare topic with the direct method topic prefix
	 * `$iothub/methods/POST/`.
	 */
	return strncmp(TOPIC_DIRECT_METHODS, topic,
		       MIN(sizeof(TOPIC_DIRECT_METHODS) - 2, topic_len)) == 0;
}

static bool is_device_twin_update(const char *topic, size_t topic_len)
{
	/* Compare topic with the device twin desired prefix
	 * `$iothub/twin/PATCH/properties/desired/#`.
	 */
	return strncmp(TOPIC_TWIN_DESIRED, topic,
		       MIN(sizeof(TOPIC_TWIN_DESIRED) - 2, topic_len)) == 0;
}

static bool is_device_twin_result(const char *topic, size_t topic_len)
{
	/* Compare topic with the device twin report result prefix
	 * `$iothub/twin/res/#`.
	 */
	return strncmp(TOPIC_TWIN_RES, topic,
		       MIN(sizeof(TOPIC_TWIN_RES) - 2, topic_len)) == 0;
}

/* @brief Get report result's status code.
 *
 * @param status Pointer to status.
 * @param rid Pointer to request ID.
 *
 * @return 0 on success, or -1 on failure.
 */
static int get_device_twin_result(char *topic, size_t topic_len,
				  struct azure_iot_hub_result *result)
{
	char *max_ptr = topic + topic_len - 1;
	char *ptr;

	/* Topic format:
	 *   $iothub/twin/res/{status}/?$rid={request id}&$version={version}
	 *
	 * Place pointer at start of status (integer)
	 */
	ptr = topic + sizeof("$iothub/twin/res/") - 1;

	/* Get the status as a string */
	ptr = strtok(ptr, "/");
	if ((ptr == NULL) || (strlen(ptr) > 3)) {
		LOG_ERR("Invalid status");
		return -1;
	}

	result->status = atoi(ptr);

	/* Move pointer to start of request ID */
	ptr = ptr + strlen(ptr) + sizeof("/?$rid=") - 1;
	if (ptr > max_ptr) {
		LOG_ERR("Could not find request ID");
		return -1;
	}

	/* Get the request ID as a string */
	ptr = strtok(ptr, "&");
	if ((ptr == NULL) || (ptr > max_ptr)) {
		LOG_ERR("Invalid request ID");
		return -1;
	}

	result->rid = atoi(ptr);

	LOG_DBG("Device twin received, request ID %d, status: %d",
		result->rid, result->status);

	return 0;
}

/* Note: This function alters the content of the topic string through
 * the use of strtok().
 */
static bool direct_method_process(char *topic, size_t topic_len,
				  const char *payload, size_t payload_len)
{
	char *max_ptr = topic + topic_len - 1;
	char *ptr;
	char rid_buf[8];
	struct azure_iot_hub_evt evt = {
		.type = AZURE_IOT_HUB_EVT_DIRECT_METHOD,
		.topic.str = topic,
		.topic.len = topic_len,
		.data.method.payload = payload,
		.data.method.payload_len = payload_len,
	};

	/* Topic format: $iothub/methods/POST/{method name}/?$rid={req ID} */
	/* Place pointer at start of method name */
	ptr = topic + sizeof("$iothub/methods/POST/") - 1;

	/* Get the method name */
	ptr = strtok(ptr, "/");
	if ((ptr == NULL) || ((ptr + strlen(ptr)) > max_ptr)) {
		LOG_ERR("Invalid method name");
		return false;
	}

	evt.data.method.name = ptr;

	LOG_DBG("Direct method name: %s", log_strdup(evt.data.method.name));

	/* Move pointer to start of request ID */
	ptr = ptr + strlen(ptr) + sizeof("/?$rid=") - 1;
	if ((ptr > max_ptr) || ((max_ptr - ptr) > sizeof(rid_buf))) {
		LOG_ERR("Invalid request ID");
		return false;
	}

	/* Get request ID */
	memcpy(rid_buf, ptr, max_ptr - ptr + 1);
	rid_buf[max_ptr - ptr + 1] = '\0';
	evt.data.method.rid = atoi(rid_buf);

	LOG_DBG("Direct method request ID: %d", evt.data.method.rid);

	azure_iot_hub_notify_event(&evt);

	return true;
}

static int device_twin_request(void)
{
	int err;
	ssize_t len;
	char buf[40];
	struct azure_iot_hub_data msg = {
		.topic.str = buf,
		.topic.type = AZURE_IOT_HUB_TOPIC_TWIN_REQUEST,
		.len = 0,
		.ptr = NULL,
	};

	len = snprintk(buf, sizeof(buf), TOPIC_TWIN_REQUEST, k_uptime_get_32());
	if ((len < 0) || (len > sizeof(buf))) {
		LOG_ERR("Failed to create device twin request");
		return -EMSGSIZE;
	}

	msg.topic.len = len;

	err = azure_iot_hub_send(&msg);
	if (err) {
		LOG_ERR("Failed to send device twin request, error: %d", err);
		return err;
	}

	return 0;
}

static void mqtt_evt_handler(struct mqtt_client *const c,
			     const struct mqtt_evt *mqtt_evt)
{
	int err;
	struct azure_iot_hub_evt evt = { 0 };
	bool notify = false;

	// TODO: Clean up function and break it up to make it more readable

	switch (mqtt_evt->type) {
	case MQTT_EVT_CONNACK:
		if (mqtt_evt->param.connack.return_code !=
		    MQTT_CONNECTION_ACCEPTED) {
			LOG_ERR("Connection was rejected with return code %d",
				mqtt_evt->param.connack.return_code);
			LOG_WRN("Is the device certificate valid?");
				return;
		}

		LOG_DBG("MQTT client connected");

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
		if (dps_reg_in_progress()) {
			err = dps_subscribe();
			evt.type = AZURE_IOT_HUB_EVT_DPS_STARTED;
		} else
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */
		{
			err = topic_subscribe();
			evt.type = AZURE_IOT_HUB_EVT_CONNECTED;
		}

		if (err) {
			LOG_ERR("Failed to request subscription, error: %d",
				err);
		}

		notify = true;
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT_EVT_DISCONNECT: result = %d", mqtt_evt->result);

		evt.type = AZURE_IOT_HUB_EVT_DISCONNECTED;

		k_sem_give(&disconnected);

		if (atomic_get(&dps_disconnecting) == 1) {
			break;
		}

		notify = true;
		break;
	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &mqtt_evt->param.publish;
		const char *topic = p->message.topic.topic.utf8;
		size_t topic_len = p->message.topic.topic.size;
		size_t payload_len = p->message.payload.len;

		LOG_DBG("MQTT_EVT_PUBLISH: id = %d len = %d ",
			p->message_id,
			payload_len);

		err = publish_get_payload(c, payload_len);
		if (err) {
			LOG_ERR("publish_get_payload, error: %d", err);
			break;
		}

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id
			};

			mqtt_publish_qos1_ack(c, &ack);
		}

		evt.type = AZURE_IOT_HUB_EVT_DATA_RECEIVED;
		evt.data.msg.ptr = payload_buf;
		evt.data.msg.len = payload_len;
		evt.topic.str = (char *)topic;
		evt.topic.len = topic_len;

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
		if (dps_reg_in_progress()) {
			if (dps_process_message(&evt)) {
				/* The message has been processed */
				break;
			}
		}
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

		/* Check if it's a direct method invocation */
		if (is_direct_method(topic, topic_len)) {
			if (direct_method_process((char *)topic, topic_len,
						  payload_buf, payload_len)) {
				LOG_DBG("Direct method processed");
				break;
			}

			LOG_WRN("Unhandled direct method invocation");
			break;
		}
		/* Check if message is a desired device twin change */
		if (is_device_twin_update(topic, topic_len)) {
#if IS_ENABLED(CONFIG_AZURE_FOTA)
			// TODO: Check if it's a FOTA request before processing?
			err = azure_fota_msg_process(payload_buf, payload_len);
			if (err < 0) {
				LOG_ERR("Failed to process FOTA message");
				break;
			} else if (err == 1) {
				LOG_DBG("Device twin update handled (FOTA)");
				return;
			}
#endif /* IS_ENABLED(CONFIG_AZURE_FOTA) */
			evt.type = AZURE_IOT_HUB_EVT_TWIN_DESIRED;
			notify = true;
		} else if (is_device_twin_result( topic, topic_len)) {
			LOG_DBG("Device twin data received");

			err = get_device_twin_result((char *)topic, topic_len,
						     &evt.data.result);
			if (err) {
				LOG_ERR("Failed to process report result");
				break;
			}

			/* Status codes
			 *	200: Response to request for device twin from
			 *	     the device.
			 *	204: Successful update of the device twin's
			 *	     "reported" object.
			 *	400: Bad request, malformed JSON?
			 *	429: Too many requests, check IoT Hub throttle
			 *	     settings.
			 *	5xx: Server errors
			 */
			switch (evt.data.result.status) {
			case 200:
#if IS_ENABLED(CONFIG_AZURE_FOTA)
				err = azure_fota_msg_process(payload_buf,
							     payload_len);
				if (err < 0) {
					LOG_ERR("Failed to process FOTA msg");
					return;
				} else if (err == 1) {
					LOG_DBG("FOTA message handled");
					return;
				}
#endif /* IS_ENABLED(CONFIG_AZURE_FOTA) */

				evt.type = AZURE_IOT_HUB_EVT_TWIN;
				break;
			case 204:
				evt.type =
					AZURE_IOT_HUB_EVT_TWIN_RESULT_SUCCESS;
				break;
			case 400:
				LOG_DBG("Bad twin request, malformed JSON?");
				evt.type = AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL;
				break;
			case 429:
				LOG_DBG("Too many requests");
				evt.type = AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL;
				break;
			default:
				evt.type = AZURE_IOT_HUB_EVT_TWIN_RESULT_FAIL;
				break;
			}
		}

		notify = true;

		break;
	};
	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT_EVT_PUBACK: id = %d result = %d",
			mqtt_evt->param.puback.message_id,
			mqtt_evt->result);
		break;
	case MQTT_EVT_SUBACK:
		LOG_DBG("MQTT_EVT_SUBACK: id = %d result = %d",
			mqtt_evt->param.suback.message_id,
			mqtt_evt->result);

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
		if (dps_reg_in_progress()) {
			int err = dps_send_reg_request();

			if (err) {
				LOG_ERR("DPS registration not sent, error: %d",
					err);
			} else {
				LOG_DBG("DPS registration request sent");
			}
		}
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */
		if (IS_ENABLED(CONFIG_AZURE_IOT_HUB_AUTO_DEVICE_TWIN_REQUEST)) {
			(void)device_twin_request();
		}

		evt.type = AZURE_IOT_HUB_EVT_READY;
		notify = true;
		break;
	default:
		break;
	}

	if (!notify) {
		return;
	}

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
	/* Only notify about DPS events while registration is in process */
	if (!dps_reg_in_progress() ||
	    (evt.type == AZURE_IOT_HUB_EVT_DPS_STARTED))
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */
	{
		azure_iot_hub_notify_event(&evt);
	}

}

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
static struct mqtt_utf8 *user_name_get(void)
{
	ssize_t len;

	len = snprintk(user_name_buf, sizeof(user_name_buf), USER_NAME_TEMPLATE,
		       dps_hostname_get(), device_id_buf);
	if ((len < 0) || (len > sizeof(user_name_buf))) {
		LOG_ERR("Failed to create user name");
		return NULL;
	}
	user_name.size = len;
	return &user_name;
}
#endif

#if defined(CONFIG_AZURE_IOT_HUB_STATIC_IPV4)
static int broker_init(bool dps)
{
	struct sockaddr_in *broker4 =
		((struct sockaddr_in *)&broker);

	inet_pton(AF_INET, CONFIG_AZURE_IOT_HUB_STATIC_IPV4_ADDR,
		  &broker->sin_addr);
	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(CONFIG_AZURE_IOT_HUB_PORT);

	LOG_DBG("IPv4 Address %s",
		log_strdup(CONFIG_AZURE_IOT_HUB_STATIC_IPV4_ADDR));

	return 0;
}
#else
static int broker_init(bool dps)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AZURE_IOT_HUB_AF_FAMILY,
		.ai_socktype = SOCK_STREAM
	};

	if (dps) {
#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
		err = getaddrinfo(CONFIG_AZURE_IOT_HUB_DPS_HOSTNAME,
				  NULL, &hints, &result);
		if (err) {
			LOG_ERR("getaddrinfo, error %d", err);
			return -ECHILD;
		}
#else
		LOG_ERR("DPS is not enabled");

		return -ENOTSUP;
#endif /* CONFIG_AZURE_IOT_HUB_DPS */
	} else {
		if (IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)) {
			err = getaddrinfo(dps_hostname_get(),
					  NULL, &hints, &result);
		} else {
			err = getaddrinfo(CONFIG_AZURE_IOT_HUB_HOSTNAME,
					  NULL, &hints, &result);
		}
	}

	if (err) {
		LOG_ERR("getaddrinfo, error %d", err);
		return -ECHILD;
	}

	addr = result;

	while (addr != NULL) {
		if ((addr->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    (AZURE_IOT_HUB_AF_FAMILY == AF_INET)) {
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
				->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_AZURE_IOT_HUB_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr,
				  sizeof(ipv4_addr));
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv4_addr));
			break;
		} else if ((addr->ai_addrlen == sizeof(struct sockaddr_in6)) &&
			   (AZURE_IOT_HUB_AF_FAMILY == AF_INET6)) {
			struct sockaddr_in6 *broker6 =
				((struct sockaddr_in6 *)&broker);
			char ipv6_addr[NET_IPV6_ADDR_LEN];

			memcpy(broker6->sin6_addr.s6_addr,
			       ((struct sockaddr_in6 *)addr->ai_addr)
			       ->sin6_addr.s6_addr,
			       sizeof(struct in6_addr));
			broker6->sin6_family = AF_INET6;
			broker6->sin6_port = htons(CONFIG_AZURE_IOT_HUB_PORT);

			inet_ntop(AF_INET6, &broker6->sin6_addr.s6_addr,
				  ipv6_addr, sizeof(ipv6_addr));
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv6_addr));
			break;
		}

		LOG_DBG("ai_addrlen = %u should be %u or %u",
			(unsigned int)addr->ai_addrlen,
			(unsigned int)sizeof(struct sockaddr_in),
			(unsigned int)sizeof(struct sockaddr_in6));

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);

	return err;
}
#endif /* !defined(CONFIG_AZURE_IOT_HUB_STATIC_IPV4) */

static int client_broker_init(struct mqtt_client *const client, bool dps)
{
	int err;
	static sec_tag_t sec_tag_list[] = { CONFIG_AZURE_IOT_HUB_SEC_TAG };
	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;

	mqtt_client_init(client);

	err = broker_init(dps);
	if (err) {
		return err;
	}

	/* The following parameters differ between DPS and IoT Hub:
	 *	client->user_name
	 *	tls_cfg->hostname
	 */

	client->broker			= &broker;
	client->evt_cb			= mqtt_evt_handler;
	client->client_id.utf8		= device_id_buf;
	client->client_id.size		= strlen(device_id_buf);
	client->password		= NULL;
	client->protocol_version	= MQTT_VERSION_3_1_1;
	client->rx_buf			= rx_buffer;
	client->rx_buf_size		= sizeof(rx_buffer);
	client->tx_buf			= tx_buffer;
	client->tx_buf_size		= sizeof(tx_buffer);
	client->transport.type		= MQTT_TRANSPORT_SECURE;

	tls_cfg->peer_verify		= 2; /* 2: Peer verification required */
	// tls_cfg->cipher_count		= 0;
	// tls_cfg->cipher_list		= NULL;
	tls_cfg->sec_tag_count		= ARRAY_SIZE(sec_tag_list);
	tls_cfg->sec_tag_list		= sec_tag_list;
	// tls_cfg->session_cache		= TLS_SESSION_CACHE_ENABLED;

#ifdef CONFIG_BOARD_QEMU_X86
	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		err = tls_credential_add(CONFIG_AZURE_IOT_HUB_SEC_TAG,
					 TLS_CREDENTIAL_CA_CERTIFICATE,
					 ca_certificate,
					 sizeof(ca_certificate));
		if (err < 0) {
			LOG_ERR("Failed to register public certificate: %d",
				err);
			return err;
		}

		err = tls_credential_add(CONFIG_AZURE_IOT_HUB_SEC_TAG,
					 TLS_CREDENTIAL_PRIVATE_KEY,
					 private_key,
					 sizeof(private_key));
		if (err < 0) {
			LOG_ERR("Failed to register private key: %d", err);
			return err;
		}

		err = tls_credential_add(
			CONFIG_AZURE_IOT_HUB_SEC_TAG,
			TLS_CREDENTIAL_SERVER_CERTIFICATE,
			device_certificate,
			sizeof(device_certificate));
		if (err < 0) {
			LOG_ERR("Failed to register public certificate: %d",
				err);
			return err;
		}
	}
#endif

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
	if (dps_get_reg_state() == DPS_STATE_REGISTERING) {
		ssize_t len;
		static char dps_user_name_buf[100];
		static struct mqtt_utf8 dps_user_name = {
			.utf8 = dps_user_name_buf,
		};

		len = snprintk(dps_user_name_buf,
			       sizeof(dps_user_name_buf),
			       DPS_USER_NAME, conn_config.device_id);
		if ((len < 0) || len > sizeof(dps_user_name_buf)) {
			LOG_ERR("Failed to set DPS user name");
			return -EFAULT;
		}

		client->user_name = &dps_user_name;
		client->user_name->size = len;
		tls_cfg->hostname 	= CONFIG_AZURE_IOT_HUB_DPS_HOSTNAME;

		LOG_DBG("Using DPS configuration for MQTT connection");
	} else {
		struct mqtt_utf8 *user_name_ptr = user_name_get();

		if (user_name_ptr == NULL) {
			LOG_ERR("No user name set, aborting");
			return -EFAULT;
		}

		tls_cfg->hostname = dps_hostname_get();
		client->user_name = user_name_get();
	}
#else /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */
	tls_cfg->hostname = CONFIG_AZURE_IOT_HUB_HOSTNAME;
	client->user_name = &user_name;
#endif/* !IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

	return 0;
}

static int device_id_set(const struct azure_iot_hub_config *const cfg,
			 const bool use_dps)
{
#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
	if (use_dps) {
		int err;
		char *id = dps_reg_id_get();
		size_t id_len = id == NULL ? 0 : strlen(id);

		if (id_len == 0) {
			LOG_INF("Using device ID as DPS registration ID");
		} else if (id_len > sizeof(device_id_buf)) {
			LOG_ERR("Registration ID too big for buffer");
			return -E2BIG;
		} else {
			memcpy(conn_config.device_id, id, id_len);
			conn_config.device_id_len = id_len;

			return 0;
		}

		err = dps_reg_id_set(conn_config.device_id,
				     conn_config.device_id_len);
		if (err) {
			LOG_ERR("Failed to set DPS registration ID, error: %d",
				err);
			return err;
		}

		LOG_DBG("Device ID has been set as DPS registration ID");
	}
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */
	return 0;
}

static int connect_client(struct azure_iot_hub_config *cfg)
{
	int err;
	bool use_dps =
		IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) &&
		(dps_get_reg_state() != DPS_STATE_REGISTERED);
	struct azure_iot_hub_evt evt = {
		.type = AZURE_IOT_HUB_EVT_CONNECTING,
	};


#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
	if (use_dps) {
		err = dps_start();
		if (err == -EALREADY) {
			use_dps = false;
			LOG_INF("The device is already registered to IoT Hub");
		} else if (err == -EFAULT) {
			LOG_ERR("Failed to start DPS");
			return err;
		}
	}
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

	err = device_id_set(cfg, use_dps);
	if (err) {
		LOG_ERR("Could not set device ID");
		return err;
	}

	err = client_broker_init(&client, use_dps);
	if (err) {
		LOG_ERR("client_broker_init, error: %d", err);
		return err;
	}

	azure_iot_hub_notify_event(&evt);

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect, error: %d", err);
		return err;
	}

	conn_config.socket = client.transport.tls.sock;

	k_sem_give(&connection_poll_sem);

	return conn_config.socket;
}

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
static void dps_handler(enum dps_reg_state state)
{
	int err;
	struct azure_iot_hub_evt evt;

	switch (state) {
	case DPS_STATE_REGISTERED:
		evt.type = AZURE_IOT_HUB_EVT_DPS_DONE;
		azure_iot_hub_notify_event(&evt);
		break;
	case DPS_STATE_NOT_STARTED:
	case DPS_STATE_REGISTERING:
	case DPS_STATE_FAILED:
		LOG_ERR("The DPS registration was not successful");
		evt.type = AZURE_IOT_HUB_EVT_DPS_FAILED;
		azure_iot_hub_notify_event(&evt);
		break;
	default:
		LOG_WRN("Unknown DPS state: %d", state);
		break;
	}

	LOG_DBG("Disconnecting from DPS server");

	atomic_set(&dps_disconnecting, 1);

	// TODO: Do this in different context?
	err = azure_iot_hub_disconnect();
	if (err) {
		LOG_ERR("Failed to disconnect from DPS, error: %d",
			err);
		LOG_WRN("Trying to connect to Azure IoT Hub anyway");
	} else {
		LOG_INF("Waiting maximum 5 seconds for DPS to disconnect...");
		err = k_sem_take(&disconnected, K_SECONDS(5));
		if (err) {
			LOG_ERR("DPS did not disconnect in time");
			LOG_WRN("Will connect to IoT Hub anyway");
		} else {
			LOG_DBG("DPS successfully disconnected");
		}

		if (state == DPS_STATE_REGISTERED) {
			LOG_DBG("Connecting to assigned IoT Hub (%s)",
				log_strdup(dps_hostname_get()));

			// TODO: The returned socket from here is never returned
			//	 to the application
			err = connect_client(&conn_config);
			if (err < 0) {
				LOG_ERR("Failed connection to IoT Hub, error: %d",
					err);
			}
		}
	}

	atomic_set(&dps_disconnecting, 0);
}
#endif


#if IS_ENABLED(CONFIG_AZURE_FOTA)
static int fota_progess_report(uint32_t progress)
{
	int err;
	char buf[100];
	ssize_t len;
	static uint32_t prev_progress;
	struct azure_iot_hub_data msg = {
		.topic.type = AZURE_IOT_HUB_TOPIC_TWIN_REPORT,
		.ptr = buf,
	};

	if (progress > AZURE_FOTA_EVT_DL_COMPLETE_VAL ||
	    progress < 0) {
		    LOG_DBG("Invalid progress value: %d", progress);
		    return -EINVAL;
	}

	if ((progress != 0) && (progress == prev_progress)) {
		LOG_DBG("Progress equal to previous value, will not report");
		return 0;
	}

	if ((progress != 0) &&
	    (progress != AZURE_FOTA_EVT_DL_COMPLETE_VAL) &&
	    (progress - prev_progress < FOTA_MIN_REPORT_PROGRESS_STEP)) {
		return 0;
	}

	if (progress == AZURE_FOTA_EVT_DL_COMPLETE_VAL) {
		len = snprintk(buf, sizeof(buf), FOTA_PROGRESS_REPORT,
			       "applying", progress);
	} else {
		len = snprintk(buf, sizeof(buf), FOTA_PROGRESS_REPORT,
			       "downloading", progress);
	}

	if ((len < 0) || (len > sizeof(buf))) {
		LOG_ERR("Failed to create progress report");
		return -ENOMEM;
	}

	msg.len = len;

	err = azure_iot_hub_send(&msg);
	if (err) {
		LOG_ERR("Failed to send progress report, error: %d", err);
		return err;
	}

	prev_progress = progress;

	return 0;
}

static void fota_evt_handler(struct azure_fota_event *fota_evt)
{
	struct azure_iot_hub_evt evt = { 0 };

	if (fota_evt == NULL) {
		return;
	}

	switch (fota_evt->type) {
	case AZURE_FOTA_EVT_START:
		LOG_DBG("AZURE_FOTA_EVT_START");
		evt.type = AZURE_IOT_HUB_EVT_FOTA_START;
		fota_progess_report(0);
		azure_iot_hub_notify_event(&evt);
		break;
	case AZURE_FOTA_EVT_DONE:
		LOG_DBG("AZURE_FOTA_EVT_DONE");
		evt.type = AZURE_IOT_HUB_EVT_FOTA_DONE;
		azure_iot_hub_notify_event(&evt);
		break;
	case AZURE_FOTA_EVT_ERASE_PENDING:
		LOG_DBG("AZURE_FOTA_EVT_ERASE_PENDING");
		evt.type = AZURE_IOT_HUB_EVT_FOTA_ERASE_PENDING;
		azure_iot_hub_notify_event(&evt);
		break;
	case AZURE_FOTA_EVT_ERASE_DONE:
		LOG_DBG("AZURE_FOTA_EVT_ERASE_DONE");
		evt.type = AZURE_IOT_HUB_EVT_FOTA_ERASE_DONE;
		azure_iot_hub_notify_event(&evt);
		break;
	case AZURE_FOTA_EVT_ERROR:
		LOG_ERR("AZURE_FOTA_EVT_ERROR");
		break;
	case AZURE_FOTA_EVT_DL_PROGRESS:
		LOG_DBG("AZURE_FOTA_EVT_DL_PROGRESS");

		fota_progess_report(fota_evt->dl.progress);

		// TODO: Update device twin with status

		break;
	default:
		LOG_ERR("Unknown FOTA event");
		break;
	}
}
#endif /* CONFIG_AZURE_FOTA */

/* Public interface */

int azure_iot_hub_ping(void)
{
	return mqtt_live(&client);
}

int azure_iot_hub_keepalive_time_left(void)
{
	return (int)mqtt_keepalive_time_left(&client);
}

int azure_iot_hub_input(void)
{
	return mqtt_input(&client);
}

int azure_iot_hub_send(const struct azure_iot_hub_data *const tx_data)
{
	ssize_t len;
	static char topic[100];
	struct mqtt_publish_param param = {
		.message.payload.data = tx_data->ptr,
		.message.payload.len = tx_data->len,
		.message.topic.topic.utf8 = tx_data->topic.str,
		.message.topic.topic.size = tx_data->topic.len,
	};

	// TODO expand the below topic handling

	switch (tx_data->topic.type)
	{
	case AZURE_IOT_HUB_TOPIC_EVENT:
		len = snprintk(topic, sizeof(topic),
				TOPIC_EVENTS, device_id_buf);
		if ((len < 0) || (len > sizeof(topic))) {
			LOG_ERR("Failed to create event topic");
			return -ENOMEM;
		}

		param.message.topic.topic.size = len;
		param.message.topic.topic.utf8 = topic;
		break;
	case AZURE_IOT_HUB_TOPIC_TWIN_REPORT:
		len = snprintk(topic, sizeof(topic),
				TOPIC_TWIN_REPORT, k_uptime_get_32());
		if ((len < 0) || (len > sizeof(topic))) {
			LOG_ERR("Failed to create twin report topic");
			return -ENOMEM;
		}

		param.message.topic.topic.size = len;
		param.message.topic.topic.utf8 = topic;
		break;
	case AZURE_IOT_HUB_TOPIC_TWIN_REQUEST:
		/* No special treatment needed */
		break;
	default:
		LOG_ERR("No topic specified");
		return -ENOMSG;
	}

	LOG_DBG("Publishing to topic: %s",
		log_strdup(param.message.topic.topic.utf8));

	return mqtt_publish(&client, &param);
}

int azure_iot_hub_disconnect(void)
{
	return mqtt_disconnect(&client);
}

int azure_iot_hub_connect(void)
{
	if (!is_initialized) {
		LOG_ERR("Azure IoT Hub is not initialized");
		return -ENOTSUP;
	}

	// TODO: Ref TG91-279
	//	 Returning socket works by accident because DPS and IoT Hub
	//	 happen to get the same fd. This is not guaranteed.
	//       connect_client() is called twice when DPS is used, but only
	//       the first DPS socket is returned here.
	//	 The simplest solution is to only offer internal polling of the
	//	 socket, and not expose it at all. Another solution is to
	//	 document that the correct socket must be read out of the
	//	 config struct used with azure_iot_hub_int(), and that the
	//	 user must handle cases where the fd changes from DPS
	//	 connection to the IoT hub connection.
	return connect_client(&conn_config);
}

int azure_iot_hub_init(const struct azure_iot_hub_config *config,
		       azure_iot_hub_evt_handler_t event_handler)
{
	int err;

	if (event_handler == NULL) {
		LOG_ERR("Event handler must be provided");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_AZURE_IOT_HUB_DEVICE_ID_APP)) {
		if (config == NULL) {
			LOG_ERR("Configuration must be provided");
			return -EINVAL;
		} else if (config->device_id_len >= sizeof(device_id_buf)) {
			LOG_ERR("Device ID is too long, maximum length is %d",
				CONFIG_AZURE_IOT_HUB_DEVICE_ID_MAX_LEN);
			return -EMSGSIZE;
		} else if (config->device_id == NULL) {
			LOG_ERR("Client ID not set in the application");
			return -EINVAL;
		}

		memcpy(conn_config.device_id, config->device_id,
		       config->device_id_len);
		conn_config.device_id_len = config->device_id_len;
	}

#if IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS)
	err = dps_init(&client, dps_handler);
	if (err) {
		LOG_ERR("Failed to initialize DPS, error: %d", err);
		return err;
	}

	LOG_DBG("DPS initialized");
#endif /* IS_ENABLED(CONFIG_AZURE_IOT_HUB_DPS) */

#if IS_ENABLED(CONFIG_AZURE_FOTA)
	err = azure_fota_init(fota_evt_handler);
	if (err) {
		LOG_ERR("Failed to initialize Azure FOTA, error: %d", err);
		return err;
	}

	LOG_DBG("Azure FOTA initialized");
#endif

	evt_handler = event_handler;
	is_initialized = true;
	(void)err;

	return 0;
}

int azure_iot_hub_method_respond(struct azure_iot_hub_result *result)
{
	ssize_t len;
	static char topic[100];
	struct mqtt_publish_param param = {
		.message.payload.data = result->payload,
		.message.payload.len = result->payload_len,
		.message.topic.topic.utf8 = topic,
	};

	len = snprintk(topic, sizeof(topic), TOPIC_DIRECT_METHOD_RES,
			result->status, result->rid);
	if ((len < 0) || (len > sizeof(topic))) {
		LOG_ERR("Failed to create method result topic");
		return -ENOMEM;
	}

	param.message.topic.topic.size = len;

	LOG_DBG("Publishing to topic: %s",
		log_strdup(param.message.topic.topic.utf8));

	return mqtt_publish(&client, &param);
}

#if defined(CONFIG_CLOUD_API)

/* Azure Iot Hub event handler implemenation */
static void api_event_handler(const struct azure_iot_hub_evt *evt)
{
	struct cloud_backend_config *config = azure_iot_hub_backend->config;
	struct cloud_event cloud_evt = { 0 };

	switch (evt->type) {
	case AZURE_IOT_HUB_EVT_CONNECTED:
		cloud_evt.type = CLOUD_EVT_CONNECTED;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);

		break;
	case AZURE_IOT_HUB_EVT_DISCONNECTED:
		cloud_evt.type = CLOUD_EVT_DISCONNECTED;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	case AZURE_IOT_HUB_EVT_READY:
		cloud_evt.type = CLOUD_EVT_READY;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	case AZURE_IOT_HUB_EVT_DATA_RECEIVED:
		cloud_evt.type = CLOUD_EVT_DATA_RECEIVED;
		cloud_evt.data.msg.buf = evt->data.msg.ptr;
		cloud_evt.data.msg.len = evt->data.msg.len;
		cloud_evt.data.msg.endpoint.type = CLOUD_EP_TOPIC_MSG;
		cloud_evt.data.msg.endpoint.str = evt->topic.str;
		cloud_evt.data.msg.endpoint.len = evt->topic.len;

		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
	case AZURE_IOT_HUB_EVT_FOTA_START:
		cloud_evt.type = CLOUD_EVT_FOTA_START;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	case AZURE_IOT_HUB_EVT_FOTA_DONE:
		cloud_evt.type = CLOUD_EVT_FOTA_DONE;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	case AZURE_IOT_HUB_EVT_FOTA_ERASE_PENDING:
		cloud_evt.type = CLOUD_EVT_FOTA_ERASE_PENDING;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	case AZURE_IOT_HUB_EVT_FOTA_ERASE_DONE:
		cloud_evt.type = CLOUD_EVT_FOTA_ERASE_DONE;
		cloud_notify_event(azure_iot_hub_backend, &cloud_evt,
				   config->user_data);
		break;
	default:
		break;
	}
}

static int api_init(const struct cloud_backend *const backend,
		    cloud_evt_handler_t handler)
{
	struct azure_iot_hub_config config = {
		.device_id = backend->config->id,
		.device_id_len = backend->config->id_len
	};

	azure_iot_hub_backend = (struct cloud_backend *)backend;
	azure_iot_hub_backend->config->handler = handler;

	return azure_iot_hub_init(&config, api_event_handler);
}

static int api_connect(const struct cloud_backend *const backend)
{
	int err;

	err = azure_iot_hub_connect();
	if (ret >= 0) {
		backend->config->socket = conn_config.socket;
		return CLOUD_CONNECT_RES_SUCCESS;
	}

	switch (err) {
	case -ECHILD:
		return CLOUD_CONNECT_RES_ERR_NETWORK;
	case -EACCES:
		return CLOUD_CONNECT_RES_ERR_NOT_INITD;
	case -ENOEXEC:
		return CLOUD_CONNECT_RES_ERR_BACKEND;
	case -EINVAL:
		return CLOUD_CONNECT_RES_ERR_PRV_KEY;
	case -EOPNOTSUPP:
		return CLOUD_CONNECT_RES_ERR_CERT;
	case -ECONNREFUSED:
		return CLOUD_CONNECT_RES_ERR_CERT_MISC;
	case -ETIMEDOUT:
		return CLOUD_CONNECT_RES_ERR_TIMEOUT_NO_DATA;
	case -ENOMEM:
		return CLOUD_CONNECT_RES_ERR_NO_MEM;
	default:
		LOG_DBG("azure_iot_hub_connect failed %d", err);
		return CLOUD_CONNECT_RES_ERR_MISC;
	}
}

static int api_disconnect(const struct cloud_backend *const backend)
{
	return azure_iot_hub_disconnect();
}

static int api_send(const struct cloud_backend *const backend,
		  const struct cloud_msg *const msg)
{
	struct azure_iot_hub_data tx_data = {
		.ptr = msg->buf,
		.len = msg->len,
		.qos = msg->qos,
		.topic.str = msg->endpoint.str,
		.topic.len = msg->endpoint.len,
	};

	/* TODO: Add logic to select topic based on provided string or type */

	// switch (msg->endpoint.type) {
	// case CLOUD_EP_TOPIC_STATE:
	// 	tx_data.topic.str = get_topic;
	// 	tx_data.topic.len = strlen(get_topic);
	// 	break;
	// case CLOUD_EP_TOPIC_MSG:
	// 	tx_data.topic.str = update_topic;
	// 	tx_data.topic.len = strlen(update_topic);
	// 	break;
	// case CLOUD_EP_TOPIC_STATE_DELETE:
	// 	tx_data.topic.str = delete_topic;
	// 	tx_data.topic.len = strlen(delete_topic);
	// 	break;
	// default:
	// 	if (msg->endpoint.str == NULL || msg->endpoint.len == 0) {
	// 		LOG_ERR("No application topic present in msg");
	// 		return -ENODATA;
	// 	}

	// 	tx_data.topic.str = msg->endpoint.str;
	// 	tx_data.topic.len = msg->endpoint.len;
	// 	break;
	// }

	return azure_iot_hub_send(&tx_data);
}

static int api_input(const struct cloud_backend *const backend)
{
	return azure_iot_hub_input();
}

static int api_ping(const struct cloud_backend *const backend)
{
	return azure_iot_hub_ping();
}

static int api_keepalive_time_left(const struct cloud_backend *const backend)
{
	return azure_iot_hub_keepalive_time_left();
}

static const struct cloud_api azure_iot_hub_api = {
	.init			= api_init,
	.connect		= api_connect,
	.disconnect		= api_disconnect,
	.send			= api_send,
	.ping			= api_ping,
	.keepalive_time_left	= api_keepalive_time_left,
	.input			= api_input,
};

CLOUD_BACKEND_DEFINE(AZURE_IOT_HUB, azure_iot_hub_api);
#endif

void azure_iot_hub_run(void)
{
	int ret;
	struct pollfd fds[1];
start:
	k_sem_take(&connection_poll_sem, K_FOREVER);

	fds[0].fd = conn_config.socket;
	fds[0].events = POLLIN;

	while (true) {
		ret = poll(fds, ARRAY_SIZE(fds),
			mqtt_keepalive_time_left(&client));
		if (ret == 0) {
			if (mqtt_keepalive_time_left(&client) < 1000) {
				azure_iot_hub_ping();
			}
			continue;
		}

		if ((fds[0].revents & POLLIN) == POLLIN) {
			azure_iot_hub_input();
			continue;
		}

		if (atomic_get(&dps_disconnecting) == 1) {
			/* It's expected to fail when disconnecting from the
			 * DPS server and switching to the IoT Hub broker.
			 */
			LOG_DBG("Ignored socket event during DPS disconnect");
			continue;
		}

		if (ret < 0) {
			LOG_ERR("poll() returned an error: %d", -errno);
			goto start;
		}

		if ((fds[0].revents & POLLNVAL) == POLLNVAL) {
			LOG_DBG("Socket error: POLLNVAL");
			LOG_DBG("The cloud socket was unexpectedly closed.");
			goto start;
		}

		if ((fds[0].revents & POLLHUP) == POLLHUP) {
			LOG_DBG("Socket error: POLLHUP");
			LOG_DBG("Connection was closed by the cloud.");
			goto start;
		}

		if ((fds[0].revents & POLLERR) == POLLERR) {
			LOG_DBG("Socket error: POLLERR");
			LOG_DBG("Cloud connection was unexpectedly closed.");
			goto start;
		}
	}
}

#define POLL_THREAD_STACK_SIZE 2560
K_THREAD_DEFINE(connection_poll_thread, POLL_THREAD_STACK_SIZE,
		azure_iot_hub_run, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
