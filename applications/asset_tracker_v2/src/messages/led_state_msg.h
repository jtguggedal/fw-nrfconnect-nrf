/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _LED_STATE_MSG_H_
#define _LED_STATE_MSG_H_

/**
 * @brief Led State Event
 * @defgroup asset Tracker Led State Event
 * @{
 */

#include <zephyr/kernel.h>

/** @brief Asset Tracker led states in the application. */
enum led_state {
	LED_STATE_LTE_CONNECTING,
	LED_STATE_LOCATION_SEARCHING,
	LED_STATE_CLOUD_PUBLISHING,
	LED_STATE_CLOUD_CONNECTING,
	LED_STATE_CLOUD_ASSOCIATING,
	LED_STATE_CLOUD_ASSOCIATED,
	LED_STATE_ACTIVE_MODE,
	LED_STATE_PASSIVE_MODE,
	LED_STATE_ERROR_SYSTEM_FAULT,
	LED_STATE_FOTA_UPDATING,
	LED_STATE_FOTA_UPDATE_REBOOT,
	LED_STATE_TURN_OFF,
	LED_STATE_COUNT
};

/** @brief Led state event. */
struct led_state_event {
	enum led_state state;
};

/** @} */

#endif /* _LED_STATE_MSG_H_ */
