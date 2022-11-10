/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <date_time.h>
#if defined(CONFIG_DATA_GRANT_SEND_ON_CONNECTION_QUALITY)
#include <modem/lte_lc.h>
#endif
#include <modem/modem_info.h>
#if defined(CONFIG_NRF_CLOUD_AGPS)
#include <net/nrf_cloud_agps.h>
#endif

#if defined(CONFIG_NRF_CLOUD_PGPS)
#include <net/nrf_cloud_pgps.h>
#endif

#include "cloud_codec/cloud_codec.h"

#define MODULE data_module

#include "module_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DATA_MODULE_LOG_LEVEL);

#define DEVICE_SETTINGS_KEY			"data_module"
#define DEVICE_SETTINGS_CONFIG_KEY		"config"

/* Data module super states. */
static enum state_type {
	STATE_CLOUD_DISCONNECTED,
	STATE_CLOUD_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Ringbuffers. All data received by the Data module are stored in ringbuffers.
 * Upon a LTE connection loss the device will keep sampling/storing data in
 * the buffers, and empty the buffers in batches upon a reconnect.
 */
static struct cloud_data_gnss gnss_buf[CONFIG_DATA_GNSS_BUFFER_COUNT];
static struct cloud_data_sensors sensors_buf[CONFIG_DATA_SENSOR_BUFFER_COUNT];
static struct cloud_data_ui ui_buf[CONFIG_DATA_UI_BUFFER_COUNT];
static struct cloud_data_impact impact_buf[CONFIG_DATA_IMPACT_BUFFER_COUNT];
static struct cloud_data_battery bat_buf[CONFIG_DATA_BATTERY_BUFFER_COUNT];
static struct cloud_data_modem_dynamic modem_dyn_buf[CONFIG_DATA_MODEM_DYNAMIC_BUFFER_COUNT];
static struct cloud_data_neighbor_cells neighbor_cells;

/* Static modem data does not change between firmware versions and does not
 * have to be buffered.
 */
static struct cloud_data_modem_static modem_stat;
/* Size of the static modem (modem_stat) data structure.
 * Used to provide an array size when encoding batch data.
 */
#define MODEM_STATIC_ARRAY_SIZE 1

/* Head of ringbuffers. */
static int head_gnss_buf;
static int head_sensor_buf;
static int head_modem_dyn_buf;
static int head_ui_buf;
static int head_impact_buf;
static int head_bat_buf;

static K_SEM_DEFINE(config_load_sem, 0, 1);

/* Default device configuration. */
static struct cloud_data_cfg current_cfg = {
	.location_timeout	 = CONFIG_DATA_LOCATION_TIMEOUT_SECONDS,
	.active_mode		 = IS_ENABLED(CONFIG_DATA_DEVICE_MODE_ACTIVE),
	.active_wait_timeout	 = CONFIG_DATA_ACTIVE_TIMEOUT_SECONDS,
	.movement_resolution	 = CONFIG_DATA_MOVEMENT_RESOLUTION_SECONDS,
	.movement_timeout	 = CONFIG_DATA_MOVEMENT_TIMEOUT_SECONDS,
	.accelerometer_activity_threshold	= CONFIG_DATA_ACCELEROMETER_ACT_THRESHOLD,
	.accelerometer_inactivity_threshold	= CONFIG_DATA_ACCELEROMETER_INACT_THRESHOLD,
	.accelerometer_inactivity_timeout	= CONFIG_DATA_ACCELEROMETER_INACT_TIMEOUT_SECONDS,
	.no_data.gnss		 = !IS_ENABLED(CONFIG_DATA_SAMPLE_GNSS_DEFAULT),
	.no_data.neighbor_cell	 = !IS_ENABLED(CONFIG_DATA_SAMPLE_NEIGHBOR_CELLS_DEFAULT)
};

static struct k_work_delayable data_send_work;

/* List used to keep track of responses from other modules with data that is
 * requested to be sampled/published.
 */
static enum app_module_data_type req_type_list[APP_DATA_COUNT];

/* Total number of data types requested for a particular sample/publish
 * cycle.
 */
static int recv_req_data_count;

/* Counter of data types received from other modules. When this number
 * matches the affirmed_data_type variable all requested data has been
 * received by the Data module.
 */
static int req_data_count;

/* List of data types that are supported to be sent based on LTE connection evaluation. */
enum coneval_supported_data_type {
	UNUSED,
	GENERIC,
	BATCH,
	NEIGHBOR_CELLS,
	COUNT,
};

/* Whether `agps_request_buffer` has A-GPS request buffered for sending when connection to
 * cloud has been re-established.
 */
bool agps_request_buffered;

/* Buffered A-GPS request. */
struct nrf_modem_gnss_agps_data_frame agps_request_buffer;

/* Data module message queue. */
#define DATA_QUEUE_ENTRY_COUNT		10
#define DATA_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(data_module_msgq, sizeof(struct module_msg),
	      DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "data",
	.msg_q = &data_module_msgq,
	.supports_shutdown = true,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *data_module = &self;

/* Forward declarations */
static void data_send_work_fn(struct k_work *work);
static int config_settings_handler(const char *key, size_t len,
				   settings_read_cb read_cb, void *cb_arg);
static void new_config_handle(struct cloud_data_cfg *new_config);

/* Static handlers */
SETTINGS_STATIC_HANDLER_DEFINE(MODULE, DEVICE_SETTINGS_KEY, NULL,
			       config_settings_handler, NULL, NULL);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_CLOUD_DISCONNECTED:
		return "STATE_CLOUD_DISCONNECTED";
	case STATE_CLOUD_CONNECTED:
		return "STATE_CLOUD_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
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

static bool grant_send(enum coneval_supported_data_type type,
		       struct lte_lc_conn_eval_params *coneval,
		       bool override)
{
#if defined(CONFIG_DATA_GRANT_SEND_ON_CONNECTION_QUALITY)
	/* List used to keep track of how many times a data type has been denied a send.
	 * Indexed by coneval_supported_data_type.
	 */
	static uint8_t send_denied_count[COUNT];

	if (override) {
		/* The override flag is set, grant send. */
		return true;
	}

	if (send_denied_count[type] >= CONFIG_DATA_SEND_ATTEMPTS_COUNT_MAX) {
		/* Grant send if a message has been attempted too many times. */
		LOG_WRN("Too many attempts, granting send");
		goto grant;
	}

	LOG_DBG("Current LTE energy estimate: %d", coneval->energy_estimate);

	switch (type) {
	case GENERIC:
		if (IS_ENABLED(CONFIG_DATA_GENERIC_UPDATES_ENERGY_THRESHOLD_EXCESSIVE) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EXCESSIVE) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_GENERIC_UPDATES_ENERGY_THRESHOLD_INCREASED) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_INCREASED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_GENERIC_UPDATES_ENERGY_THRESHOLD_NORMAL) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_NORMAL) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_GENERIC_UPDATES_ENERGY_THRESHOLD_REDUCED) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_REDUCED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_GENERIC_UPDATES_ENERGY_THRESHOLD_EFFICIENT) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EFFICIENT) {
			goto grant;
		}
		break;
	case NEIGHBOR_CELLS:
		if (IS_ENABLED(CONFIG_DATA_NEIGHBOR_CELL_UPDATES_ENERGY_THRESHOLD_EXCESSIVE) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EXCESSIVE) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_NEIGHBOR_CELL_UPDATES_ENERGY_THRESHOLD_INCREASED)
			   && coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_INCREASED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_NEIGHBOR_CELL_UPDATES_ENERGY_THRESHOLD_NORMAL) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_NORMAL) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_NEIGHBOR_CELL_UPDATES_ENERGY_THRESHOLD_REDUCED) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_REDUCED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_NEIGHBOR_CELL_UPDATES_ENERGY_THRESHOLD_EFFICIENT)
			   && coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EFFICIENT) {
			goto grant;
		}
		break;
	case BATCH:
		if (IS_ENABLED(CONFIG_DATA_BATCH_UPDATES_ENERGY_THRESHOLD_EXCESSIVE) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EXCESSIVE) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_BATCH_UPDATES_ENERGY_THRESHOLD_INCREASED) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_INCREASED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_BATCH_UPDATES_ENERGY_THRESHOLD_NORMAL) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_NORMAL) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_BATCH_UPDATES_ENERGY_THRESHOLD_REDUCED) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_REDUCED) {
			goto grant;
		} else if (IS_ENABLED(CONFIG_DATA_BATCH_UPDATES_ENERGY_THRESHOLD_EFFICIENT) &&
		    coneval->energy_estimate >= LTE_LC_ENERGY_CONSUMPTION_EFFICIENT) {
			goto grant;
		}
		break;
	default:
		LOG_WRN("Invalid/unsupported data type: %d", type);
		return false;
	}

	LOG_DBG("Send NOT granted, type: %d, energy estimate: %d, attempt: %d", type,
		coneval->energy_estimate, send_denied_count[type]);
	send_denied_count[type]++;
	return false;

grant:
	LOG_DBG("Send granted, type: %d, energy estimate: %d, attempt: %d",
		type, coneval->energy_estimate, send_denied_count[type]);
	send_denied_count[type] = 0;
#endif /* CONFIG_DATA_GRANT_SEND_ON_CONNECTION_QUALITY */
	return true;
}

static int config_settings_handler(const char *key, size_t len,
				   settings_read_cb read_cb, void *cb_arg)
{
	int err = 0;

	if (strcmp(key, DEVICE_SETTINGS_CONFIG_KEY) == 0) {
		err = read_cb(cb_arg, &current_cfg, sizeof(current_cfg));
		if (err < 0) {
			LOG_ERR("Failed to load configuration, error: %d", err);
		} else {
			LOG_DBG("Device configuration loaded from flash");
			err = 0;
		}
	}

	k_sem_give(&config_load_sem);
	return err;
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
		/* Fall through. */
	case DATE_TIME_OBTAINED_NTP:
		/* Fall through. */
	case DATE_TIME_OBTAINED_EXT: {
		// TODO: Decide how to handle errors. Is this really critical?
		SEND_MSG(cloud_module, DATA_MSG_DATE_TIME_OBTAINED);

		/* De-register handler. At this point the application will have
		 * date time to depend on indefinitely until a reboot occurs.
		 */
		date_time_register_handler(NULL);
		break;
	}
	case DATE_TIME_NOT_OBTAINED:
		break;
	default:
		break;
	}
}

static int save_config(const void *buf, size_t buf_len)
{
	int err;

	err = settings_save_one(DEVICE_SETTINGS_KEY "/"
				DEVICE_SETTINGS_CONFIG_KEY,
				buf, buf_len);
	if (err) {
		LOG_WRN("settings_save_one, error: %d", err);
		return err;
	}

	LOG_DBG("Device configuration stored to flash");

	return 0;
}

static void cloud_codec_event_handler(const struct cloud_codec_evt *evt)
{
	if (evt->type == CLOUD_CODEC_EVT_CONFIG_UPDATE) {
		new_config_handle((struct cloud_data_cfg *)&evt->config_update);
	} else {
		LOG_ERR("Unknown cloud codec event");
	}
}

static int setup(void)
{
	int err;

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init, error: %d", err);
		return err;
	}

	err = settings_load_subtree(DEVICE_SETTINGS_KEY);
	if (err) {
		LOG_ERR("settings_load_subtree, error: %d", err);
		return err;
	}

	/* Wait up to 1 seconds for the settings API to load the device configuration stored
	 * to flash, if any.
	 */
	if (k_sem_take(&config_load_sem, K_SECONDS(1)) != 0) {
		LOG_DBG("Failed retrieveing the device configuration from flash in time");
	}

	err = cloud_codec_init(&current_cfg, cloud_codec_event_handler);
	if (err) {
		LOG_ERR("cloud_codec_init, error: %d", err);
		return err;
	}

	date_time_register_handler(date_time_event_handler);
	return 0;
}

static void config_print_all(void)
{
	if (current_cfg.active_mode) {
		LOG_DBG("Device mode: Active");
	} else {
		LOG_DBG("Device mode: Passive");
	}

	LOG_DBG("Active wait timeout: %d", current_cfg.active_wait_timeout);
	LOG_DBG("Movement resolution: %d", current_cfg.movement_resolution);
	LOG_DBG("Movement timeout: %d", current_cfg.movement_timeout);
	LOG_DBG("Location timeout: %d", current_cfg.location_timeout);
	LOG_DBG("Accelerometer act threshold: %.2f",
		 current_cfg.accelerometer_activity_threshold);
	LOG_DBG("Accelerometer inact threshold: %.2f",
		 current_cfg.accelerometer_inactivity_threshold);
	LOG_DBG("Accelerometer inact timeout: %.2f",
		 current_cfg.accelerometer_inactivity_timeout);

	if (!current_cfg.no_data.neighbor_cell) {
		LOG_DBG("Requesting of neighbor cell data is enabled");
	} else {
		LOG_DBG("Requesting of neighbor cell data is disabled");
	}

	if (!current_cfg.no_data.gnss) {
		LOG_DBG("Requesting of GNSS data is enabled");
	} else {
		LOG_DBG("Requesting of GNSS data is disabled");
	}
}

static void config_distribute(enum module_msg_type type)
{
	int err;
	struct module_msg msg = {
		.type = type,
		.data.cfg = current_cfg,
	};

	err = module_send_msg_all(&msg);
	if (err) {
		LOG_ERR("Failed to distribute configuration");
	}
}

static void data_send(enum module_msg_type type,
		      struct cloud_codec_data *data)
{
	int err;
	struct module_msg msg = {
		.type = type,
		.data.cfg = current_cfg,
	};

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LWM2M)) {
		memcpy(msg.data.buffer.paths, data->paths, sizeof(data->paths));
		msg.data.buffer.valid_object_paths = data->valid_object_paths;
	} else {
		msg.data.buffer.buf = data->buf;
		msg.data.buffer.len = data->len;
	}

	err = module_send_msg(cloud_module, &msg);
	if (err) {
		LOG_ERR("Failed to distribute configuration");
	}

	if (IS_ENABLED(CONFIG_DEBUG_MODULE)) {
		err = module_send_msg(debug_module, &msg);
		if (err) {
			LOG_ERR("Failed to distribute configuration");
		}
	}

	/* Reset buffer */
	memset(data, 0, sizeof(struct cloud_codec_data));
}

/* This function allocates buffer on the heap, which needs to be freed after use. */
static void data_encode(void)
{
	int err;
	struct cloud_codec_data codec = { 0 };
	struct lte_lc_conn_eval_params coneval = { 0 };

	/* Variable used to override connection evaluation calculations in case connection
	 * evalution fails for some non-critical reason.
	 */
	bool override = false;

	if (!date_time_is_valid()) {
		/* Date time library does not have valid time to
		 * timestamp cloud data. Abort cloud publicaton. Data will
		 * be cached in it respective ringbuffer.
		 */
		return;
	}

#if defined(CONFIG_DATA_GRANT_SEND_ON_CONNECTION_QUALITY)
	/* Perform connection evaluation to determine how expensive it is to send data. */
	err = lte_lc_conn_eval_params_get(&coneval);
	if (err < 0) {
		LOG_ERR("lte_lc_conn_eval_params_get, error: %d", err);
		SEND_ERROR(CLOUD_MSG_ERROR, err);
		return;
	} else if (err > 0) {
		LOG_WRN("Connection evaluation failed, error: %d", err);

		/* Positive error codes returned from lte_lc_conn_eval_params_get() indicates
		 * that the connection evaluation failed due to a non-critical reason.
		 * We don't treat this as an irrecoverable error because it can occur
		 * occasionally. Since we don't have any connection evaluation parameters we
		 * grant encoding and sending of data.
		 */
		override = true;
	}
#endif

	if (grant_send(NEIGHBOR_CELLS, &coneval, override)) {
		err = cloud_codec_encode_neighbor_cells(&codec, &neighbor_cells);
		switch (err) {
		case 0:
			LOG_DBG("Neighbor cell data encoded successfully");
			data_send(DATA_MSG_NEIGHBOR_CELLS_DATA_SEND, &codec);
			break;
		case -ENOTSUP:
			/* Neighbor cell data encoding not supported */
			break;
		case -ENODATA:
			LOG_DBG("No neighbor cells data to encode, error: %d", err);
			break;
		default:
			LOG_ERR("Error encoding neighbor cells data: %d", err);
			SEND_ERROR(DATA_MSG_ERROR, err);
			return;
		}
	}

	if (grant_send(GENERIC, &coneval, override)) {
		err = cloud_codec_encode_data(&codec,
					      &gnss_buf[head_gnss_buf],
					      &sensors_buf[head_sensor_buf],
					      &modem_stat,
					      &modem_dyn_buf[head_modem_dyn_buf],
					      &ui_buf[head_ui_buf],
					      &impact_buf[head_impact_buf],
					      &bat_buf[head_bat_buf]);
		switch (err) {
		case 0:
			LOG_DBG("Data encoded successfully");
			data_send(DATA_MSG_DATA_SEND, &codec);
			break;
		case -ENODATA:
			/* This error might occur when data has not been obtained prior
			 * to data encoding.
			 */
			LOG_DBG("No new data to encode");
			break;
		case -ENOTSUP:
			LOG_DBG("Regular data updates are not supported");
			break;
		default:
			LOG_ERR("Error encoding message %d", err);
			SEND_ERROR(DATA_MSG_ERROR, err);
			return;
		}
	}

	if (grant_send(BATCH, &coneval, override)) {
		err = cloud_codec_encode_batch_data(&codec,
						    gnss_buf,
						    sensors_buf,
						    &modem_stat,
						    modem_dyn_buf,
						    ui_buf,
						    impact_buf,
						    bat_buf,
						    ARRAY_SIZE(gnss_buf),
						    ARRAY_SIZE(sensors_buf),
						    MODEM_STATIC_ARRAY_SIZE,
						    ARRAY_SIZE(modem_dyn_buf),
						    ARRAY_SIZE(ui_buf),
						    ARRAY_SIZE(impact_buf),
						    ARRAY_SIZE(bat_buf));
		switch (err) {
		case 0:
			LOG_DBG("Batch data encoded successfully");
			data_send(DATA_MSG_DATA_SEND_BATCH, &codec);
			break;
		case -ENODATA:
			LOG_DBG("No batch data to encode, ringbuffers are empty");
			break;
		case -ENOTSUP:
			LOG_DBG("Encoding of batch data not supported");
			break;
		default:
			LOG_ERR("Error batch-enconding data: %d", err);
			SEND_ERROR(DATA_MSG_ERROR, err);
			return;
		}
	}
}

#if defined(CONFIG_NRF_CLOUD_AGPS) && !defined(CONFIG_NRF_CLOUD_MQTT)
static int get_modem_info(struct modem_param_info *const modem_info)
{
	__ASSERT_NO_MSG(modem_info != NULL);

	int err = modem_info_init();

	if (err) {
		LOG_ERR("Could not initialize modem info module, error: %d", err);
		return err;
	}

	err = modem_info_params_init(modem_info);
	if (err) {
		LOG_ERR("Could not initialize modem info parameters, error: %d", err);
		return err;
	}

	err = modem_info_params_get(modem_info);
	if (err) {
		LOG_ERR("Could not obtain cell information, error: %d", err);
		return err;
	}

	return 0;
}

/**
 * @brief Combine and encode modem network parameters together with the incoming A-GPS data request
 *	  types to form the A-GPS request.
 *
 * @param[in] incoming_request Pointer to a structure containing A-GPS data types that has been
 *			       requested by the modem. If incoming_request is NULL, all A-GPS data
 *			       types are requested.
 *
 * @return 0 on success, otherwise a negative error code indicating reason of failure.
 */
static int agps_request_encode(struct nrf_modem_gnss_agps_data_frame *incoming_request)
{
	int err;
	struct cloud_codec_data codec = {0};
	static struct modem_param_info modem_info = {0};
	static struct cloud_data_agps_request cloud_agps_request = {0};

	err = get_modem_info(&modem_info);
	if (err) {
		return err;
	}

	if (incoming_request == NULL) {
		const uint32_t mask = IS_ENABLED(CONFIG_NRF_CLOUD_PGPS) ? 0u : 0xFFFFFFFFu;

		LOG_DBG("Requesting all A-GPS elements");
		cloud_agps_request.request.sv_mask_ephe = mask,
		cloud_agps_request.request.sv_mask_alm = mask,
		cloud_agps_request.request.data_flags =
					NRF_MODEM_GNSS_AGPS_GPS_UTC_REQUEST |
					NRF_MODEM_GNSS_AGPS_KLOBUCHAR_REQUEST |
					NRF_MODEM_GNSS_AGPS_SYS_TIME_AND_SV_TOW_REQUEST |
					NRF_MODEM_GNSS_AGPS_POSITION_REQUEST |
					NRF_MODEM_GNSS_AGPS_INTEGRITY_REQUEST;
	} else {
		cloud_agps_request.request = *incoming_request;
	}

	cloud_agps_request.mcc = modem_info.network.mcc.value;
	cloud_agps_request.mnc = modem_info.network.mnc.value;
	cloud_agps_request.cell = modem_info.network.cellid_dec;
	cloud_agps_request.area = modem_info.network.area_code.value;
	cloud_agps_request.queued = true;
#if defined(CONFIG_LOCATION_MODULE_AGPS_FILTERED)
	cloud_agps_request.filtered = CONFIG_LOCATION_MODULE_AGPS_FILTERED;
#endif
#if defined(CONFIG_LOCATION_MODULE_ELEVATION_MASK)
	cloud_agps_request.mask_angle = CONFIG_LOCATION_MODULE_ELEVATION_MASK;
#endif

	err = cloud_codec_encode_agps_request(&codec, &cloud_agps_request);
	switch (err) {
	case 0:
		LOG_DBG("A-GPS request encoded successfully");
		data_send(DATA_MSG_AGPS_REQUEST_DATA_SEND, &codec);
		break;
	case -ENOTSUP:
		LOG_ERR("Encoding of A-GPS requests are not supported by the configured codec");
		break;
	case -ENODATA:
		LOG_DBG("No A-GPS request data to encode, error: %d", err);
		break;
	default:
		LOG_ERR("Error encoding A-GPS request: %d", err);
		SEND_ERROR(DATA_MSG_ERROR, err);
		break;
	}

	return err;
}
#endif /* CONFIG_NRF_CLOUD_AGPS && !CONFIG_NRF_CLOUD_MQTT */

static void config_get(void)
{
	SEND_MSG_ALL(DATA_MSG_CONFIG_GET);
}

static void config_send(void)
{
	int err;
	struct cloud_codec_data codec = { 0 };

	err = cloud_codec_encode_config(&codec, &current_cfg);
	if (err == -ENOTSUP) {
		LOG_WRN("Encoding of device configuration is not supported");
		return;
	} else if (err) {
		LOG_ERR("Error encoding configuration, error: %d", err);
		SEND_ERROR(DATA_MSG_ERROR, err);
		return;
	}

	data_send(DATA_MSG_CONFIG_SEND, &codec);
}

static void data_ui_send(void)
{
	int err;
	struct cloud_codec_data codec = {0};

	if (!date_time_is_valid()) {
		/* Date time library does not have valid time to
		 * timestamp cloud data. Abort cloud publicaton. Data will
		 * be cached in it respective ringbuffer.
		 */
		return;
	}

	err = cloud_codec_encode_ui_data(&codec, &ui_buf[head_ui_buf]);
	if (err == -ENODATA) {
		LOG_DBG("No new UI data to encode, error: %d", err);
		return;
	} else if (err == -ENOTSUP) {
		LOG_ERR("Encoding of UI data is not supported, error: %d", err);
		return;
	} else if (err) {
		LOG_ERR("Encoding button press, error: %d", err);
		SEND_ERROR(DATA_MSG_ERROR, err);
		return;
	}

	data_send(DATA_MSG_UI_DATA_SEND, &codec);
}

static void data_impact_send(void)
{
	int err;
	struct cloud_codec_data codec = {0};

	if (!date_time_is_valid()) {
		return;
	}

	err = cloud_codec_encode_impact_data(&codec, &impact_buf[head_impact_buf]);
	if (err == -ENODATA) {
		LOG_DBG("No new impact data to encode, error: %d", err);
		return;
	} else if (err == -ENOTSUP) {
		LOG_WRN("Encoding of impact data is not supported, error: %d", err);
		return;
	} else if (err) {
		LOG_ERR("Encoding impact data failed, error: %d", err);
		SEND_ERROR(DATA_MSG_ERROR, err);
		return;
	}

	data_send(DATA_MSG_IMPACT_DATA_SEND, &codec);
}

static void requested_data_clear(void)
{
	recv_req_data_count = 0;
	req_data_count = 0;
}

static void data_send_work_fn(struct k_work *work)
{
	(void)SEND_MSG(&self, DATA_MSG_DATA_READY);

	requested_data_clear();
	k_work_cancel_delayable(&data_send_work);
}

static void requested_data_status_set(enum app_module_data_type data_type)
{
	if (!k_work_delayable_is_pending(&data_send_work)) {
		/* If the data_send_work is not pending it means that the module has already
		 * triggered an data encode/send.
		 */
		LOG_DBG("Data already encoded and sent, abort");
		return;
	}

	for (size_t i = 0; i < recv_req_data_count; i++) {
		if (req_type_list[i] == data_type) {
			req_data_count++;
			break;
		}
	}

	if (req_data_count == recv_req_data_count) {
		data_send_work_fn(NULL);
	}
}

static void requested_data_list_set(const enum app_module_data_type *data_list,
				    size_t count)
{
	if ((count == 0) || (count > APP_DATA_COUNT)) {
		LOG_ERR("Invalid data type list length");
		return;
	}

	requested_data_clear();

	for (size_t i = 0; i < count; i++) {
		req_type_list[i] = data_list[i];
	}

	recv_req_data_count = count;
}

static void new_config_handle(struct cloud_data_cfg *new_config)
{
	bool config_change = false;

	/* Guards making sure that only new configuration values are applied. */
	if (current_cfg.active_mode != new_config->active_mode) {
		current_cfg.active_mode = new_config->active_mode;

		if (current_cfg.active_mode) {
			LOG_DBG("New Device mode: Active");
		} else {
			LOG_DBG("New Device mode: Passive");
		}

		config_change = true;
	}

	if (current_cfg.no_data.gnss != new_config->no_data.gnss) {
		current_cfg.no_data.gnss = new_config->no_data.gnss;

		if (!current_cfg.no_data.gnss) {
			LOG_DBG("Requesting of GNSS data is enabled");
		} else {
			LOG_DBG("Requesting of GNSS data is disabled");
		}

		config_change = true;
	}

	if (current_cfg.no_data.neighbor_cell != new_config->no_data.neighbor_cell) {
		current_cfg.no_data.neighbor_cell = new_config->no_data.neighbor_cell;

		if (!current_cfg.no_data.neighbor_cell) {
			LOG_DBG("Requesting of neighbor cell data is enabled");
		} else {
			LOG_DBG("Requesting of neighbor cell data is disabled");
		}

		config_change = true;
	}

	if (new_config->location_timeout > 0) {
		if (current_cfg.location_timeout != new_config->location_timeout) {
			current_cfg.location_timeout = new_config->location_timeout;

			LOG_DBG("New location timeout: %d", current_cfg.location_timeout);

			config_change = true;
		}
	} else {
		LOG_WRN("New location timeout out of range: %d", new_config->location_timeout);
	}

	if (new_config->active_wait_timeout > 0) {
		if (current_cfg.active_wait_timeout != new_config->active_wait_timeout) {
			current_cfg.active_wait_timeout = new_config->active_wait_timeout;

			LOG_DBG("New Active wait timeout: %d", current_cfg.active_wait_timeout);

			config_change = true;
		}
	} else {
		LOG_WRN("New Active timeout out of range: %d", new_config->active_wait_timeout);
	}

	if (new_config->movement_resolution > 0) {
		if (current_cfg.movement_resolution != new_config->movement_resolution) {
			current_cfg.movement_resolution = new_config->movement_resolution;

			LOG_DBG("New Movement resolution: %d", current_cfg.movement_resolution);

			config_change = true;
		}
	} else {
		LOG_WRN("New Movement resolution out of range: %d",
			new_config->movement_resolution);
	}

	if (new_config->movement_timeout > 0) {
		if (current_cfg.movement_timeout != new_config->movement_timeout) {
			current_cfg.movement_timeout = new_config->movement_timeout;

			LOG_DBG("New Movement timeout: %d", current_cfg.movement_timeout);

			config_change = true;
		}
	} else {
		LOG_WRN("New Movement timeout out of range: %d", new_config->movement_timeout);
	}

	if (current_cfg.accelerometer_activity_threshold !=
	    new_config->accelerometer_activity_threshold) {
		current_cfg.accelerometer_activity_threshold =
		new_config->accelerometer_activity_threshold;
		LOG_DBG("New Accelerometer act threshold: %.2f",
			current_cfg.accelerometer_activity_threshold);
		config_change = true;
	}
	if (current_cfg.accelerometer_inactivity_threshold !=
	    new_config->accelerometer_inactivity_threshold) {
		current_cfg.accelerometer_inactivity_threshold =
		new_config->accelerometer_inactivity_threshold;
		LOG_DBG("New Accelerometer inact threshold: %.2f",
			current_cfg.accelerometer_inactivity_threshold);
		config_change = true;
	}
	if (current_cfg.accelerometer_inactivity_timeout !=
	    new_config->accelerometer_inactivity_timeout) {
		current_cfg.accelerometer_inactivity_timeout =
		new_config->accelerometer_inactivity_timeout;
		LOG_DBG("New Accelerometer inact timeout: %.2f",
			current_cfg.accelerometer_inactivity_timeout);
		config_change = true;
	}

	/* If there has been a change in the currently applied device configuration we want to store
	 * the configuration to flash and distribute it to other modules.
	 */
	if (config_change) {
		int err = save_config(&current_cfg, sizeof(current_cfg));

		if (err) {
			LOG_ERR("Configuration not stored, error: %d", err);
		}

		config_distribute(DATA_MSG_CONFIG_READY);
	} else {
		LOG_DBG("No new values in incoming device configuration update message");
	}

	/* Always acknowledge all configurations back to cloud to avoid a potential mismatch
	 * between reported parameters in the cloud-side state and parameters reported by the
	 * device.
	 */

	/* LwM2M doesn't require reporting of the current configuration back to cloud. */
	if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
		return;
	}

	LOG_DBG("Acknowledge currently applied configuration back to cloud");
	config_send();
}

/**
 * @brief Function that requests A-GPS and P-GPS data upon receiving a request from the
 *        location module.
 *
 * @param[in] incoming_request Pointer to a structure containing A-GPS data types that has been
 *			       requested by the modem. If incoming_request is NULL, all A-GPS data
 *			       types are requested.
 */
static void agps_request_handle(const struct nrf_modem_gnss_agps_data_frame *incoming_request)
{
	int err;

#if defined(CONFIG_NRF_CLOUD_AGPS)
	struct nrf_modem_gnss_agps_data_frame request;

	if (incoming_request != NULL) {
		request.sv_mask_ephe = IS_ENABLED(CONFIG_NRF_CLOUD_PGPS) ?
				       0u : incoming_request->sv_mask_ephe;
		request.sv_mask_alm = IS_ENABLED(CONFIG_NRF_CLOUD_PGPS) ?
				       0u : incoming_request->sv_mask_alm;
		request.data_flags = incoming_request->data_flags;
	}

#if defined(CONFIG_NRF_CLOUD_MQTT)
	/* If CONFIG_NRF_CLOUD_MQTT is enabled, the nRF Cloud MQTT transport library will be used
	 * to send the request.
	 */
	err = (incoming_request == NULL) ? nrf_cloud_agps_request_all() :
					   nrf_cloud_agps_request(&request);
	if (err) {
		LOG_WRN("Failed to request A-GPS data, error: %d", err);
		LOG_DBG("This is expected to fail if we are not in a connected state");
	} else {
		if (nrf_cloud_agps_request_in_progress()) {
			LOG_DBG("A-GPS request sent");
			return;
		}
		LOG_DBG("No A-GPS data requested");
		/* Continue so P-GPS, if enabled, can be requested. */
	}
#else
	/* If the nRF Cloud MQTT transport library is not enabled, we will have to create an
	 * A-GPS request and send out a message containing the request for the cloud module to pick
	 * up and send to the cloud that is currently used.
	 */
	err = (incoming_request == NULL) ? agps_request_encode(NULL) :
					   agps_request_encode(&request);
	if (err) {
		LOG_WRN("Failed to request A-GPS data, error: %d", err);
	} else {
		LOG_DBG("A-GPS request sent");
		return;
	}
#endif
#endif

#if defined(CONFIG_NRF_CLOUD_PGPS)
	/* A-GPS data is not expected to be received. Proceed to schedule a callback when
	 * P-GPS data for current time is available.
	 */
	err = nrf_cloud_pgps_notify_prediction();
	if (err) {
		LOG_ERR("Requesting notification of prediction availability, error: %d", err);
	}
#endif

	(void)err;
}

/* Message handler for STATE_CLOUD_DISCONNECTED. */
static void on_cloud_state_disconnected(const struct module_msg *msg)
{
	if (IS_MSG(msg, CLOUD_MSG_CONNECTED)) {

		if (IS_ENABLED(CONFIG_DATA_AGPS_REQUEST_ALL_UPON_CONNECTION)) {
			agps_request_handle(NULL);
		}

		state_set(STATE_CLOUD_CONNECTED);
		if (agps_request_buffered) {
			LOG_DBG("Handle buffered A-GPS request");
			agps_request_handle(&agps_request_buffer);
			agps_request_buffered = false;
		}
		return;
	}

	if (IS_MSG(msg, CLOUD_MSG_CONFIG_EMPTY) &&
	    IS_ENABLED(CONFIG_NRF_CLOUD_MQTT)) {
		config_send();
	}

	if (IS_MSG(msg, LOCATION_MSG_AGPS_NEEDED)) {
		LOG_DBG("A-GPS request buffered");
		agps_request_buffered = true;
		agps_request_buffer = msg->location.agps_request;
		return;
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_cloud_state_connected(const struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_DATA_READY)) {
		data_encode();
		return;
	}

	if (IS_MSG(msg, APP_MSG_CONFIG_GET)) {
		config_get();
		return;
	}

	if (IS_MSG(msg, DATA_MSG_UI_DATA_READY)) {
		data_ui_send();
		return;
	}

	if (IS_MSG(msg, DATA_MSG_IMPACT_DATA_READY)) {
		data_impact_send();
		return;
	}

	if (IS_MSG(msg, CLOUD_MSG_DISCONNECTED)) {
		state_set(STATE_CLOUD_DISCONNECTED);
		return;
	}

	if (IS_MSG(msg, CLOUD_MSG_CONFIG_EMPTY)) {
		config_send();
		return;
	}

	if (IS_MSG(msg, LOCATION_MSG_AGPS_NEEDED)) {
		agps_request_handle(NULL);
		return;
	}
}

/* Message handler for all states. */
static void on_all_states(const struct module_msg *msg)
{
	/* Distribute new configuration received from cloud. */
	if (IS_MSG(msg, CLOUD_MSG_CONFIG_RECEIVED)) {
		struct cloud_data_cfg new = {
			.active_mode = msg->cloud.config.active_mode,
			.active_wait_timeout = msg->cloud.config.active_wait_timeout,
			.movement_resolution = msg->cloud.config.movement_resolution,
			.movement_timeout = msg->cloud.config.movement_timeout,
			.location_timeout = msg->cloud.config.location_timeout,
			.accelerometer_activity_threshold =
				msg->cloud.config.accelerometer_activity_threshold,
			.accelerometer_inactivity_threshold =
				msg->cloud.config.accelerometer_inactivity_threshold,
			.accelerometer_inactivity_timeout =
				msg->cloud.config.accelerometer_inactivity_timeout,
			.no_data.gnss = msg->cloud.config.no_data.gnss,
			.no_data.neighbor_cell = msg->cloud.config.no_data.neighbor_cell
		};

		new_config_handle(&new);
		return;
	}

	if (IS_MSG(msg, LOCATION_MSG_AGPS_NEEDED)) {
		agps_request_handle(&msg->location.agps_request);
		return;
	}

	if (IS_MSG(msg, APP_MSG_START)) {
		config_print_all();
		config_distribute(DATA_MSG_CONFIG_INIT);
	}

	if (IS_MSG(msg, UTIL_MSG_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(DATA_MSG_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if (IS_MSG(msg, APP_MSG_DATA_GET)) {
		/* Store which data is requested by the app, later to be used
		 * to confirm data is reported to the data manager.
		 */
		requested_data_list_set(msg->app.data_list, msg->app.count);

		/* Start countdown until data must have been received by the
		 * Data module in order to be sent to cloud
		 */
		k_work_reschedule(&data_send_work, K_SECONDS(msg->app.timeout));

		return;
	}

	if (IS_MSG(msg, UI_MSG_BUTTON_DATA_READY)) {
		struct cloud_data_ui new_ui_data = {
			.btn = msg->ui.btn.button_number,
			.btn_ts = msg->ui.btn.timestamp,
			.queued = true
		};

		cloud_codec_populate_ui_buffer(ui_buf, &new_ui_data,
					       &head_ui_buf,
					       ARRAY_SIZE(ui_buf));

		(void)SEND_MSG(&self, DATA_MSG_UI_DATA_READY);
		return;
	}

	if (IS_MSG(msg, MODEM_MSG_MODEM_STATIC_DATA_NOT_READY)) {
		requested_data_status_set(APP_DATA_MODEM_STATIC);
	}

	if (IS_MSG(msg, MODEM_MSG_MODEM_STATIC_DATA_READY)) {
		modem_stat.ts = msg->modem.modem_static.timestamp;
		modem_stat.queued = true;

		BUILD_ASSERT(sizeof(modem_stat.appv) >=
			     sizeof(msg->modem.modem_static.app_version));
		BUILD_ASSERT(sizeof(modem_stat.brdv) >=
			     sizeof(msg->modem.modem_static.board_version));
		BUILD_ASSERT(sizeof(modem_stat.fw) >= sizeof(msg->modem.modem_static.modem_fw));
		BUILD_ASSERT(sizeof(modem_stat.iccid) >= sizeof(msg->modem.modem_static.iccid));
		BUILD_ASSERT(sizeof(modem_stat.imei) >= sizeof(msg->modem.modem_static.imei));

		(void)strcpy(modem_stat.appv, msg->modem.modem_static.app_version);
		(void)strcpy(modem_stat.brdv, msg->modem.modem_static.board_version);
		(void)strcpy(modem_stat.fw, msg->modem.modem_static.modem_fw);
		(void)strcpy(modem_stat.iccid, msg->modem.modem_static.iccid);
		(void)strcpy(modem_stat.imei, msg->modem.modem_static.imei);

		requested_data_status_set(APP_DATA_MODEM_STATIC);
	}

	if (IS_MSG(msg, MODEM_MSG_MODEM_DYNAMIC_DATA_NOT_READY)) {
		requested_data_status_set(APP_DATA_MODEM_DYNAMIC);
	}

	if (IS_MSG(msg, MODEM_MSG_MODEM_DYNAMIC_DATA_READY)) {
		struct cloud_data_modem_dynamic new_modem_data = {
			.area = msg->modem.modem_dynamic.area_code,
			.nw_mode = msg->modem.modem_dynamic.nw_mode,
			.band = msg->modem.modem_dynamic.band,
			.cell = msg->modem.modem_dynamic.cell_id,
			.rsrp = msg->modem.modem_dynamic.rsrp,
			.mcc = msg->modem.modem_dynamic.mcc,
			.mnc = msg->modem.modem_dynamic.mnc,
			.ts = msg->modem.modem_dynamic.timestamp,

			.area_code_fresh = msg->modem.modem_dynamic.area_code_fresh,
			.nw_mode_fresh = msg->modem.modem_dynamic.nw_mode_fresh,
			.band_fresh = msg->modem.modem_dynamic.band_fresh,
			.cell_id_fresh = msg->modem.modem_dynamic.cell_id_fresh,
			.rsrp_fresh = msg->modem.modem_dynamic.rsrp_fresh,
			.ip_address_fresh = msg->modem.modem_dynamic.ip_address_fresh,
			.mccmnc_fresh = msg->modem.modem_dynamic.mccmnc_fresh,
			.queued = true
		};

		BUILD_ASSERT(sizeof(new_modem_data.ip) >=
			     sizeof(msg->modem.modem_dynamic.ip_address));

		BUILD_ASSERT(sizeof(new_modem_data.apn) >=
			     sizeof(msg->modem.modem_dynamic.apn));

		BUILD_ASSERT(sizeof(new_modem_data.apn) >=
			     sizeof(msg->modem.modem_dynamic.apn));

		(void)strcpy(new_modem_data.ip, msg->modem.modem_dynamic.ip_address);
		(void)strcpy(new_modem_data.apn, msg->modem.modem_dynamic.apn);
		(void)strcpy(new_modem_data.mccmnc, msg->modem.modem_dynamic.mccmnc);

		cloud_codec_populate_modem_dynamic_buffer(
						modem_dyn_buf,
						&new_modem_data,
						&head_modem_dyn_buf,
						ARRAY_SIZE(modem_dyn_buf));

		requested_data_status_set(APP_DATA_MODEM_DYNAMIC);
	}

	if (IS_MSG(msg, MODEM_MSG_BATTERY_DATA_NOT_READY)) {
		requested_data_status_set(APP_DATA_BATTERY);
	}

	if (IS_MSG(msg, MODEM_MSG_BATTERY_DATA_READY)) {
		struct cloud_data_battery new_battery_data = {
			.bat = msg->modem.bat.battery_voltage,
			.bat_ts = msg->modem.bat.timestamp,
			.queued = true
		};

		cloud_codec_populate_bat_buffer(bat_buf, &new_battery_data,
						&head_bat_buf,
						ARRAY_SIZE(bat_buf));

		requested_data_status_set(APP_DATA_BATTERY);
	}

	if (IS_MSG(msg, SENSOR_MSG_ENVIRONMENTAL_DATA_READY)) {
		struct cloud_data_sensors new_sensor_data = {
			.temperature = msg->sensor.sensors.temperature,
			.humidity = msg->sensor.sensors.humidity,
			.pressure = msg->sensor.sensors.pressure,
			.bsec_air_quality = msg->sensor.sensors.bsec_air_quality,
			.env_ts = msg->sensor.sensors.timestamp,
			.queued = true
		};

		cloud_codec_populate_sensor_buffer(sensors_buf,
						   &new_sensor_data,
						   &head_sensor_buf,
						   ARRAY_SIZE(sensors_buf));

		requested_data_status_set(APP_DATA_ENVIRONMENTAL);
	}

	if (IS_MSG(msg, SENSOR_MSG_ENVIRONMENTAL_NOT_SUPPORTED)) {
		requested_data_status_set(APP_DATA_ENVIRONMENTAL);
	}

	if (IS_MSG(msg, SENSOR_MSG_MOVEMENT_IMPACT_DETECTED)) {
		struct cloud_data_impact new_impact_data = {
			.magnitude = msg->sensor.impact.magnitude,
			.ts = msg->sensor.impact.timestamp,
			.queued = true
		};

		cloud_codec_populate_impact_buffer(impact_buf, &new_impact_data,
						   &head_impact_buf,
						   ARRAY_SIZE(impact_buf));

		(void)SEND_MSG(&self, DATA_MSG_IMPACT_DATA_READY);
		return;
	}

	if (IS_MSG(msg, LOCATION_MSG_GNSS_DATA_READY)) {
		struct cloud_data_gnss new_location_data = {
			.gnss_ts = msg->location.location.timestamp,
			.queued = true
		};

		new_location_data.pvt.acc = msg->location.location.pvt.accuracy;
		new_location_data.pvt.alt = msg->location.location.pvt.altitude;
		new_location_data.pvt.hdg = msg->location.location.pvt.heading;
		new_location_data.pvt.lat = msg->location.location.pvt.latitude;
		new_location_data.pvt.longi = msg->location.location.pvt.longitude;
		new_location_data.pvt.spd = msg->location.location.pvt.speed;

		cloud_codec_populate_gnss_buffer(gnss_buf, &new_location_data,
						&head_gnss_buf,
						ARRAY_SIZE(gnss_buf));

		requested_data_status_set(APP_DATA_LOCATION);
	}

	if (IS_MSG(msg, LOCATION_MSG_DATA_NOT_READY)) {
		requested_data_status_set(APP_DATA_LOCATION);
	}

	if (IS_MSG(msg, LOCATION_MSG_NEIGHBOR_CELLS_DATA_READY)) {
		BUILD_ASSERT(sizeof(neighbor_cells.cell_data) ==
			     sizeof(msg->modem.neighbor_cells.cell_data));

		BUILD_ASSERT(sizeof(neighbor_cells.neighbor_cells) ==
			     sizeof(msg->modem.neighbor_cells.neighbor_cells));

		memcpy(&neighbor_cells.cell_data, &msg->modem.neighbor_cells.cell_data,
		       sizeof(neighbor_cells.cell_data));

		memcpy(&neighbor_cells.neighbor_cells,
		       &msg->modem.neighbor_cells.neighbor_cells,
		       sizeof(neighbor_cells.neighbor_cells));

		neighbor_cells.ts = msg->modem.neighbor_cells.timestamp;
		neighbor_cells.queued = true;

		requested_data_status_set(APP_DATA_NEIGHBOR_CELLS);
	}

	if (IS_MSG(msg, MODEM_MSG_NEIGHBOR_CELLS_DATA_NOT_READY)) {
		requested_data_status_set(APP_DATA_NEIGHBOR_CELLS);
	}

	if (IS_MSG(msg, LOCATION_MSG_TIMEOUT)) {
		requested_data_status_set(APP_DATA_LOCATION);
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
		SEND_ERROR(DATA_MSG_ERROR, err);
	}

	state_set(STATE_CLOUD_DISCONNECTED);

	k_work_init_delayable(&data_send_work, data_send_work_fn);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(DATA_MSG_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_CLOUD_DISCONNECTED:
			on_cloud_state_disconnected(&msg);
			break;
		case STATE_CLOUD_CONNECTED:
			on_cloud_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown sub state");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(data_module_thread, CONFIG_DATA_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
