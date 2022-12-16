/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MODULES_COMMON_H_
#define _MODULES_COMMON_H_

/**@file
 * @brief Modules common library header.
 */

#include <zephyr/kernel.h>

#include "messages/msg_definitions.h"

#define MODULE_DEFINITIONS					\
	X(APP_MODULE,	 	app_module)			\
	X(CLOUD_MODULE, 	cloud_module)			\
	X(DATA_MODULE,		data_module)			\
	X(DEBUG_MODULE,		debug_module)			\
	X(LOCATION_MODULE,	location_module)		\
	X(MODEM_MODULE,		modem_module)			\
	X(SENSOR_MODULE,	sensor_module)			\
	X(UI_MODULE,		ui_module)			\
	X(UTIL_MODULE,		util_module)

struct module_msg;

extern struct module_data *app_module;
extern struct module_data *cloud_module;
extern struct module_data *data_module;
extern struct module_data *debug_module;
extern struct module_data *location_module;
extern struct module_data *modem_module;
extern struct module_data *sensor_module;
extern struct module_data *ui_module;
extern struct module_data *util_module;

/**
 * @defgroup module_common Module common library
 * @{
 * @brief A Library that exposes generic functionality shared between application modules.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Macro that checks if a message is of a certain type.
 *
 *  @param _msg Pointer to message.
 *  @param _type Message type.
 *
 * @return true if the message matches the message checked for, otherwise false.
 */
#define IS_MSG(_msg, _type) ((_msg)->type == _type)

/** @brief Macro used to send a message without payload to a module.
 *
 *  @param _dest Pointer to destination module.
 *  @param _type Type of message.
 */
#define SEND_MSG(_dest, _type) module_send_msg(_dest, &(struct module_msg){.type = _type})

/** @brief Macro used to send a message without payload to all modules.
 *
 *  @param _type Type of message.
 */
#define SEND_MSG_ALL(_type) module_send_msg_all(&(struct module_msg){.type = _type})

/** @brief Macro used to send an error message to all modules.
 *
 *  @param _type Name of the type of error message.
 *  @param _error_code Error code.
 */
#define SEND_ERROR(_type, _error_code)  module_send_msg_all(&(struct module_msg){.type = _type})

/** @brief Macro used to send a shutdown acknowledgement to the util module.
 *
 *  @param _type Name of the type of shutdown message.
 *  @param _id ID of the module that acknowledges the shutdown.
 */
#define SEND_SHUTDOWN_ACK(_type, _id)	\
	module_send_msg(util_module, &(struct module_msg){.type = _type})

/** @brief Structure that contains module metadata. */
struct module_data {
	/* Variable used to construct a linked list of module metadata. */
	sys_snode_t header;
	/* ID specific to each module. Internally assigned when calling module_start(). */
	uint32_t id;
	/* The ID of the module thread. */
	k_tid_t thread_id;
	/* Name of the module. */
	char *name;
	/* Pointer to the internal message queue in the module. */
	struct k_msgq *msg_q;
	/* Message handler, to be used by modules that don't have thread for processing messages. */
	int (*message_handler)(struct module_msg *msg);
	/* Flag signifying if the module supports shutdown. */
	bool supports_shutdown;
};

/** @brief Purge a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
void module_purge_queue(struct module_data *module);

/** @brief Get the next message in a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[out] msg Pointer to a message buffer that the output will be written to.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_get_next_msg(struct module_data *module, void *msg);

/** @brief Enqueue message to a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[in] msg Pointer to a message that will be enqueued.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_enqueue_msg(struct module_data *module, void *msg);

/** @brief Register that a module has performed a graceful shutdown.
 *
 *  @param[in] id_reg Identifier of module.
 *
 *  @return true If this API has been called for all modules supporting graceful shutdown in the
 *	    application.
 */
bool modules_shutdown_register(uint32_t id_reg);

/** @brief Register and start a module.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_start(struct module_data *module);

/** @brief Get the number of active modules in the application.
 *
 *  @return Number of active modules in the application.
 */
uint32_t module_active_count_get(void);

/* When sending a message, the type is decided by the receiver.
 * This means that the receiver can process the data immediately without any type checking.
 */
struct module_msg {
	enum module_msg_type type;
	union {
		struct app_msg app;
		struct cloud_msg cloud;
		struct data_msg data;
		struct debug_msg debug;
		struct location_msg location;
		struct modem_msg modem;
		struct sensor_msg sensor;
		struct ui_msg ui;
		struct util_msg util;
	};
};

/** @brief Send message to a module's message queue.
 *
 *  @param[in] destination Pointer to destination module.
 *  @param[in] msg Pointer to the message to send.
 *
 *  @return 0 if successful, otherwise a negative error code.
 *  @retval -ENOTSUP If the module does not have a processing thread or message handler.
 */
int module_send_msg(struct module_data *destination, struct module_msg *msg);

/** @brief Send message to all modules in the system.
 *
 *  @param[in] msg Pointer to the message to send.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_send_msg_all(struct module_msg *msg);

/**
 *@}
 */


#ifdef __cplusplus
}
#endif

#endif /* _MODULES_COMMON_H_ */
