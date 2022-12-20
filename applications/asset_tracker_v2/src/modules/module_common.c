/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>

#include "module_common.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(module_common, CONFIG_MODULE_COMMON_LOG_LEVEL);


/* List containing metadata on active modules in the application. */
static sys_slist_t module_list = SYS_SLIST_STATIC_INIT(&module_list);
static K_MUTEX_DEFINE(module_list_lock);

/* Structure containing general information about the modules in the application. */
static struct module_info {
	/* Modules that support shutdown. */
	atomic_t shutdown_supported_count;
	/* Number of active modules in the application. */
	atomic_t active_modules_count;
} modules_info;

/* Public interface */
void module_purge_queue(struct module_data *module)
{
	__ASSERT_NO_MSG(module->msg_q);

	k_msgq_purge(module->msg_q);
}

int module_get_next_msg(struct module_data *module, void *msg)
{
	__ASSERT_NO_MSG(module->msg_q);

	return k_msgq_get(module->msg_q, msg, K_FOREVER);
}

int module_enqueue_msg(struct module_data *module, void *msg)
{
	int err;

	__ASSERT_NO_MSG(module->msg_q);

	err = k_msgq_put(module->msg_q, msg, K_NO_WAIT);
	if (err) {
		LOG_WRN("%s: Message could not be enqueued, error code: %d",
			module->name, err);
			/* Purge message queue before reporting an error. This
			 * makes sure that the calling module can
			 * enqueue and process new events and is not being
			 * blocked by a full message queue.
			 *
			 * This error is concidered irrecoverable and should be
			 * rebooted on.
			 */
			module_purge_queue(module);
		return err;
	}

	return 0;
}

bool modules_shutdown_register(uint32_t id_reg)
{
	bool retval = false;
	struct module_data *module, *next_module = NULL;

	if (id_reg == 0) {
		LOG_WRN("Passed in module ID cannot be 0");
		return false;
	}

	k_mutex_lock(&module_list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&module_list, module, next_module, header) {
		if (module->id == id_reg) {
			if (module->supports_shutdown) {
				/* A module shutdown has been registered. Decrease the number of
				 * active modules in the application and delete the list entry for
				 * the corresponding module.
				 */
				sys_slist_find_and_remove(&module_list, &module->header);
				atomic_dec(&modules_info.active_modules_count);
				atomic_dec(&modules_info.shutdown_supported_count);

				LOG_WRN("Module \"%s\" shutdown registered", module->name);
			} else {
				goto exit;
			}
			break;
		}
	};

	if (modules_info.shutdown_supported_count == 0) {
		/* All modules in the application have reported a shutdown. */
		retval = true;
	}

exit:
	k_mutex_unlock(&module_list_lock);
	return retval;
}

int module_start(struct module_data *module)
{
	if (module == NULL) {
		LOG_ERR("Module metadata is NULL");
		return -EINVAL;
	}

	if (module->name == NULL) {
		LOG_ERR("Module name is NULL");
		return -EINVAL;
	}

	module->id = k_cycle_get_32();

	atomic_inc(&modules_info.active_modules_count);

	if (module->supports_shutdown) {
		atomic_inc(&modules_info.shutdown_supported_count);
	}

	/* Append passed in module metadata to linked list. */
	k_mutex_lock(&module_list_lock, K_FOREVER);
	sys_slist_append(&module_list, &module->header);
	k_mutex_unlock(&module_list_lock);

	if (module->thread_id && module->msg_q) {
		LOG_DBG("Module \"%s\" (%p) with thread ID %p started",
			module->name, (void *)module, module->thread_id);
	} else if (module->message_handler) {
		LOG_DBG("Module \"%s\" (%p) with message handler %p started",
			module->name, (void *)module, module->message_handler);
	} else {
		LOG_WRN("Module \"%s\" (%p) started, no thread and no message handler registered",
			module->name, (void *)module);
	}

	return 0;
}

uint32_t module_active_count_get(void)
{
	return atomic_get(&modules_info.active_modules_count);
}


/* Experimental stuff */

static const char *module_msg_name_strings[] = {
#define X(_name, _data_type)				\
		[_name] = STRINGIFY(_name),
		MESSAGE_TYPES
#undef X
};

static const char *msg_type_to_str(enum module_msg_type type)
{
	__ASSERT_NO_MSG(type < ARRAY_SIZE(module_msg_name_strings));

	return module_msg_name_strings[type];
}

static char *module_thread_id_to_name(k_tid_t thread_id)
{
	struct module_data *current_module, *next_module = NULL;

	/* Protect by mutex? */

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&module_list, current_module, next_module, header) {
		if (current_module->thread_id == thread_id) {
			return current_module->name;
		}
	};

	return "Unknown module";
}

int module_send_msg(struct module_data *destination, struct module_msg *msg)
{
	LOG_DBG("%s --> %s:  \t%s",
		module_thread_id_to_name(k_current_get()),
		destination->name,
		msg_type_to_str(msg->type));

	if (destination->msg_q) {
		LOG_DBG("Message (%p) to queue %p (%s)",
			(void *)msg, (void *)destination->msg_q, destination->name);

		return module_enqueue_msg(destination, msg);
	}

	if (!destination->message_handler) {
		LOG_ERR("%s (%p) has no thread or message handler, cannot send message to it",
			destination->name, (void *)destination);

		return -ENOTSUP;
	}

	LOG_DBG("Message (%p) to handler %p (%s)",
		(void *)msg, (void *)destination->message_handler, destination->name);

	return destination->message_handler(msg);
}

int module_send_msg_all(struct module_msg *msg)
{
	int err = 0;
	struct module_data *module, *next_module = NULL;

	/* Protect by mutex? */

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&module_list, module, next_module, header) {
		err = module_send_msg(module, msg);
		if (err) {
			LOG_ERR("Failed to send message %p to module %p (%s)", msg, (void *)module,
				module->name);

			return err;
		}
	};

	return err;
}
