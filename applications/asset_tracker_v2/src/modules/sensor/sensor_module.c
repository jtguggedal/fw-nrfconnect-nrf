/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/sensor.h>

#if defined(CONFIG_EXTERNAL_SENSORS)
#include "ext_sensors.h"
#endif

#define MODULE sensor_module

#include "module_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, CONFIG_SENSOR_MODULE_LOG_LEVEL);

/* Sensor module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

/* Sensor module message queue. */
#define SENSOR_QUEUE_ENTRY_COUNT	10
#define SENSOR_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(sensor_module_msgq, sizeof(struct module_msg),
	      SENSOR_QUEUE_ENTRY_COUNT, SENSOR_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "sensor",
	.msg_q = &sensor_module_msgq,
	.supports_shutdown = true,
};

/* Workaround to let other modules know about this module without changing code here. */
struct module_data *sensor_module = &self;

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
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

/* Static module functions. */

#if defined(CONFIG_EXTERNAL_SENSORS)
/* Function that enables or disables trigger callbacks from the accelerometer. */
static void accelerometer_callback_set(bool enable)
{
	int err;

	err = ext_sensors_accelerometer_trigger_callback_set(enable);
	if (err) {
		LOG_ERR("ext_sensors_accelerometer_trigger_callback_set, error: %d", err);
	}
}

static void activity_data_send(const struct ext_sensor_evt *const acc_data)
{
	int err;
	struct module_msg msg;

	if (acc_data->type == EXT_SENSOR_MSG_ACCELEROMETER_ACT_TRIGGER)	{
		msg.type = SENSOR_MSG_MOVEMENT_ACTIVITY_DETECTED;
	} else {
		__ASSERT_NO_MSG(acc_data->type == EXT_SENSOR_MSG_ACCELEROMETER_INACT_TRIGGER);
		msg.type = SENSOR_MSG_MOVEMENT_INACTIVITY_DETECTED;
	}

	err = module_send_msg(app_module, &msg);
	if (err) {
		LOG_ERR("Failed to send impact data, error: %d", err);
	}
}

static void impact_data_send(const struct ext_sensor_evt *const evt)
{
	int err;
	struct module_msg msg = {
		.type = SENSOR_MSG_MOVEMENT_IMPACT_DETECTED,
		.sensor.impact = {
			.magnitude = evt->value,
			.timestamp - k_uptime_get(),
		},
	};

	err = module_send_msg(data_module, &msg);
	if (err) {
		LOG_ERR("Failed to send impact data, error: %d", err);
	}
}

static void ext_sensor_handler(const struct ext_sensor_evt *const evt)
{
	switch (evt->type) {
	case EXT_SENSOR_MSG_ACCELEROMETER_ACT_TRIGGER:
		activity_data_send(evt);
		break;
	case EXT_SENSOR_MSG_ACCELEROMETER_INACT_TRIGGER:
		activity_data_send(evt);
		break;
	case EXT_SENSOR_MSG_ACCELEROMETER_IMPACT_TRIGGER:
		impact_data_send(evt);
		break;
	case EXT_SENSOR_MSG_ACCELEROMETER_ERROR:
		LOG_ERR("EXT_SENSOR_MSG_ACCELEROMETER_ERROR");
		break;
	case EXT_SENSOR_MSG_TEMPERATURE_ERROR:
		LOG_ERR("EXT_SENSOR_MSG_TEMPERATURE_ERROR");
		break;
	case EXT_SENSOR_MSG_HUMIDITY_ERROR:
		LOG_ERR("EXT_SENSOR_MSG_HUMIDITY_ERROR");
		break;
	case EXT_SENSOR_MSG_PRESSURE_ERROR:
		LOG_ERR("EXT_SENSOR_MSG_PRESSURE_ERROR");
		break;
	case EXT_SENSOR_MSG_BME680_BSEC_ERROR:
		LOG_ERR("EXT_SENSOR_MSG_BME680_BSEC_ERROR");
		break;
	default:
		break;
	}
}
#endif /* CONFIG_EXTERNAL_SENSORS */

#if defined(CONFIG_EXTERNAL_SENSORS)
static void configure_acc(const struct cloud_data_cfg *cfg)
{
		int err;
		double accelerometer_activity_threshold =
			cfg->accelerometer_activity_threshold;
		double accelerometer_inactivity_threshold =
			cfg->accelerometer_inactivity_threshold;
		double accelerometer_inactivity_timeout =
			cfg->accelerometer_inactivity_timeout;

		err = ext_sensors_accelerometer_threshold_set(accelerometer_activity_threshold,
							      true);
		if (err == -ENOTSUP) {
			LOG_WRN("The requested act threshold value not valid");
		} else if (err) {
			LOG_ERR("Failed to set act threshold, error: %d", err);
		}
		err = ext_sensors_accelerometer_threshold_set(accelerometer_inactivity_threshold,
							      false);
		if (err == -ENOTSUP) {
			LOG_WRN("The requested inact threshold value not valid");
		} else if (err) {
			LOG_ERR("Failed to set inact threshold, error: %d", err);
		}
		err = ext_sensors_inactivity_timeout_set(accelerometer_inactivity_timeout);
		if (err == -ENOTSUP) {
			LOG_WRN("The requested timeout value not valid");
		} else if (err) {
			LOG_ERR("Failed to set timeout, error: %d", err);
		}
}
#endif

static void apply_config(struct module_msg *msg)
{
#if defined(CONFIG_EXTERNAL_SENSORS)
	configure_acc(&msg->data.cfg);

	if (msg->data.cfg.active_mode) {
		accelerometer_callback_set(false);
	} else {
		accelerometer_callback_set(true);
	}
#endif /* CONFIG_EXTERNAL_SENSORS */
}

static void environmental_data_get(void)
{
	int err;
	struct module_msg msg;

#if defined(CONFIG_EXTERNAL_SENSORS)
	double temperature = 0, humidity = 0, pressure = 0;
	uint16_t bsec_air_quality = UINT16_MAX;

	/* Request data from external sensors. */
	err = ext_sensors_temperature_get(&temperature);
	if (err) {
		LOG_ERR("ext_sensors_temperature_get, error: %d", err);
	}

	err = ext_sensors_humidity_get(&humidity);
	if (err) {
		LOG_ERR("ext_sensors_humidity_get, error: %d", err);
	}

	err = ext_sensors_pressure_get(&pressure);
	if (err) {
		LOG_ERR("ext_sensors_pressure_get, error: %d", err);
	}

	err = ext_sensors_air_quality_get(&bsec_air_quality);
	if (err && err == -ENOTSUP) {
		/* Air quality is not available, enable the Bosch BSEC library driver.
		 * Propagate the air quality value as -1.
		 */
	} else if (err) {
		LOG_ERR("ext_sensors_bsec_air_quality_get, error: %d", err);
	}

	sensor_module_event = new_sensor_module_event();

	__ASSERT(sensor_module_event, "Not enough heap left to allocate event");

	msg.sensor.sensors.timestamp = k_uptime_get();
	msg.sensor.sensors.temperature = temperature;
	msg.sensor.sensors.humidity = humidity;
	msg.sensor.sensors.pressure = pressure;
	msg.sensor.sensors.bsec_air_quality =
					(bsec_air_quality == UINT16_MAX) ? -1 : bsec_air_quality;
	msg.type = SENSOR_MSG_ENVIRONMENTAL_DATA_READY;
#else
	/* This event must be sent even though environmental sensors are not
	 * available on the nRF9160DK. This is because the Data module expects
	 * responses from the different modules within a certain amounf of time
	 * after the APP_MSG_DATA_GET event has been emitted.
	 */
	LOG_DBG("No external sensors, submitting dummy sensor data");

	msg.type = SENSOR_MSG_ENVIRONMENTAL_NOT_SUPPORTED;
#endif
	err = module_send_msg(data_module, &msg);
	if (err) {
		LOG_ERR("Failed to send message, error: %d", err);
	}
}

static int setup(void)
{
#if defined(CONFIG_EXTERNAL_SENSORS)
	int err;

	err = ext_sensors_init(ext_sensor_handler);
	if (err) {
		LOG_ERR("ext_sensors_init, error: %d", err);
		return err;
	}
#endif
	return 0;
}

static bool environmental_data_requested(enum app_module_data_type *data_list,
					 size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (data_list[i] == APP_DATA_ENVIRONMENTAL) {
			return true;
		}
	}

	return false;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_CONFIG_INIT)) {
		apply_config(msg);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct module_msg *msg)
{
	if (IS_MSG(msg, DATA_MSG_CONFIG_READY)) {
		apply_config(msg);
	}

	if (IS_MSG(msg, APP_MSG_DATA_GET)) {
		if (!environmental_data_requested(
			msg->app.data_list,
			msg->app.count)) {
			return;
		}

		environmental_data_get();
	}
}

/* Message handler for all states. */
static void on_all_states(struct module_msg *msg)
{
	if (IS_MSG(msg, UTIL_MSG_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(SENSOR_MSG_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
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
		SEND_ERROR(SENSOR_MSG_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(SENSOR_MSG_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_ERR("Unknown state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(sensor_module_thread, CONFIG_SENSOR_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
