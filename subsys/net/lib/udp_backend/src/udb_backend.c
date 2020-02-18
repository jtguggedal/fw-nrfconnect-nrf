#include <net/udp_backend.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <net/cloud_backend.h>
#include <stdio.h>
#include <at_cmd.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(UDP_backend, CONFIG_UDP_BACKEND_LOG_LEVEL);

static char rx_buffer[CONFIG_UDP_BACKEND_RX_TX_BUFFER_LEN];
static char tx_buffer[CONFIG_UDP_BACKEND_RX_TX_BUFFER_LEN];

static struct sockaddr_in host_addr;
static struct sockaddr_in local_addr;
static int client_fd;
static char client_id[20];

#if !defined(CONFIG_CLOUD_API)
static udp_backend_evt_handler_t module_evt_handler;
#endif

#if defined(CONFIG_CLOUD_API)
static struct cloud_backend *udp_backend_backend;
#endif

#if !defined(CONFIG_CLOUD_API)
static void udp_backend_notify_event(const struct udp_backend_evt *evt)
{
	if ((module_evt_handler != NULL) && (evt != NULL)) {
		module_evt_handler(evt);
	}
}
#endif

static int udp_init(void)
{
	int err=0;

	inet_pton(AF_INET, CONFIG_UDP_BACKEND_STATIC_IPV4_ADDR,
		  &host_addr.sin_addr);
	host_addr.sin_port = htons(CONFIG_UDP_BACKEND_PORT);
	host_addr.sin_family = AF_INET;
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(0);
	local_addr.sin_addr.s_addr = 0;
	LOG_DBG("IPv4 Address %s", log_strdup(CONFIG_UDP_BACKEND_STATIC_IPV4_ADDR));

	return err;
}

int udp_backend_ping(void)
{
	static char ping[]="";
	int err=send(client_fd, ping, 1, 0);
	if(err < 0) {
		return err;
	}
	else {
		LOG_DBG("Ping");
		return 0;
	}
}

int udp_backend_input(void)
{
	return recv(client_fd, rx_buffer, CONFIG_UDP_BACKEND_RX_TX_BUFFER_LEN, MSG_DONTWAIT);
}

int udp_backend_send(const struct udp_backend_tx_data *const tx_data)
{
	int err=send(client_fd, tx_data->str, tx_data->len, 0);
	if(err < 0) {
		if(err == -1)
		{

		}
		return err;
	}
	else {
		return 0;
	}
}

int udp_backend_disconnect(void)
{
	(void)close(client_fd);
	return 0;
}

int udp_backend_connect(struct udp_backend_config *const config)
{
	int err;
	client_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_fd < 0) {
		LOG_ERR("client_fd: %d\n\r", client_fd);
		return client_fd;
	}

	err = bind(client_fd, (struct sockaddr *)&local_addr,
		   sizeof(local_addr));
	if (err < 0) {
		LOG_ERR("bind err: %d errno: %d\n\r", err, errno);
		return err;
	}

	err = connect(client_fd, (struct sockaddr *)&host_addr,
		      sizeof(host_addr));
	if (err < 0) {
		LOG_ERR("connect err: %d errno: %d\n\r", err, errno);
		return err;
	}

	udp_backend_ping();

#if !defined(CONFIG_CLOUD_API)
	config->socket = client_fd;
#endif

	return err;
}

int udp_backend_init(const struct udp_backend_config *const config,
		 udp_backend_evt_handler_t event_handler)
{
	return udp_init();
}

#if defined(CONFIG_CLOUD_API)
static int c_init(const struct cloud_backend *const backend,
		  cloud_evt_handler_t handler)
{
	int err;
	char imei[20];

	err = at_cmd_write("AT+CGSN", imei, sizeof(imei), NULL);
	if (err) {
		LOG_ERR("Could not obtain IMEI to generate device ID, err: %d",
			err);
		return err;
	}

	memcpy(client_id, &imei[8], 7);

	/* Just to be safe */
	client_id[7] = '\0';

	LOG_DBG("UDP backend ID: %s", log_strdup(client_id));

	backend->config->handler = handler;
	udp_backend_backend = (struct cloud_backend *)backend;

	return udp_backend_init(NULL, NULL);
}

static int c_connect(const struct cloud_backend *const backend)
{
	int err;

	err = udp_backend_connect(NULL);
	if (err) {
		return err;
	}

	backend->config->socket = client_fd;
	
	struct cloud_event connected_event = {
		.type = CLOUD_EVT_CONNECTED,
	};
	backend->config->handler(backend, &connected_event, NULL);

	struct cloud_event ready_event = {
		.type = CLOUD_EVT_READY,
	};
	backend->config->handler(backend, &ready_event, NULL);

	return err;
}

static int c_disconnect(const struct cloud_backend *const backend)
{
	return udp_backend_disconnect();
}

static int c_send(const struct cloud_backend *const backend,
		  const struct cloud_msg *const msg)
{
	int prefixlen = snprintf(tx_buffer, sizeof(tx_buffer), "%s:",
				 client_id);
	memcpy(tx_buffer + prefixlen, msg->buf, msg->len);

	struct udp_backend_tx_data tx_data = {
		.str = tx_buffer,
		.len = msg->len + prefixlen,
	};

	int err= udp_backend_send(&tx_data);
	if(err == -1)
	{
		udp_backend_disconnect();
		err = udp_backend_connect(NULL);
		if (err) {
			return err;
		}

		backend->config->socket = client_fd;
		err= udp_backend_send(&tx_data);

	}
	if (!err) {
		struct cloud_event sent_event = {
			.type = CLOUD_EVT_DATA_SENT,
		};
		backend->config->handler(backend, &sent_event, NULL);
	}
	return err;
}

static int c_input(const struct cloud_backend *const backend)
{
	int err = udp_backend_input();
	if(err < 0) {
		return err;
	}
	else {
		rx_buffer[err]=0;
		LOG_DBG("RX: %s", log_strdup(rx_buffer));
		struct cloud_event recv_event = { 0 };
		recv_event.type = CLOUD_EVT_DATA_RECEIVED;
		recv_event.data.msg.buf = rx_buffer;
		recv_event.data.msg.len = err;

		backend->config->handler(backend, &recv_event, NULL);

		return 0;
	}
}

static int c_ping(const struct cloud_backend *const backend)
{
	return udp_backend_ping();
}

static int c_keepalive_time_left(const struct cloud_backend *const backend)
{
	return 50000; //FIXME
}

static const struct cloud_api udp_backend_api = {
	.init			= c_init,
	.connect		= c_connect,
	.disconnect		= c_disconnect,
	.send			= c_send,
	.ping			= c_ping,
	.keepalive_time_left	= c_keepalive_time_left,
	.input			= c_input,
	.ep_subscriptions_add	= NULL,
};

CLOUD_BACKEND_DEFINE(UDP_BACKEND, udp_backend_api);
#endif
