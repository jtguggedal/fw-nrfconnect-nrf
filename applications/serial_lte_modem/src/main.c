/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <logging/log.h>
#include <logging/log_ctrl.h>

#include <zephyr.h>
#include <stdio.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <string.h>
#include <nrf_modem.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>
#include <hal/nrf_regulators.h>
#include <hal/nrf_uarte.h>
#include <modem/nrf_modem_lib.h>
#include <modem/at_notif.h>
#include <dfu/mcuboot.h>
#include <dfu/dfu_target.h>
#include <sys/reboot.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include <mgmt/fmfu_mgmt.h>
#include <mgmt/fmfu_mgmt_stat.h>
#include "os_mgmt/os_mgmt.h"
#include "img_mgmt/img_mgmt.h"
#include "slm_at_host.h"
#include "slm_at_fota.h"
#include "slm_util.h"

LOG_MODULE_REGISTER(app, CONFIG_SLM_LOG_LEVEL);

#define SLM_DFU_STATUS          0xBC  /* Flag to be saved in GPREGRET[0]*/
#define BUTTONLESS_DFU                /* Flag to support buttonless DFU */

#define SLM_WQ_STACK_SIZE       KB(2)
#define SLM_WQ_PRIORITY         K_LOWEST_APPLICATION_THREAD_PRIO
static K_THREAD_STACK_DEFINE(slm_wq_stack_area, SLM_WQ_STACK_SIZE);

static const struct device *gpio_dev;
static struct gpio_callback gpio_cb;
static struct k_work exit_idle_work;
static bool full_idle_mode;

/* global variable used across different files */
struct k_work_q slm_work_q;

/* global variable defined in different files */
extern uint8_t fota_type;
extern uint8_t fota_stage;
extern uint8_t fota_status;
extern int32_t fota_info;

/* global functions defined in different files */
int poweron_uart(bool sync_str);
int slm_settings_init(void);
int slm_setting_fota_save(void);

/**@brief Recoverable modem library error. */
void nrf_modem_recoverable_error_handler(uint32_t err)
{
	LOG_ERR("Modem library recoverable error: %u", err);

	/* Recoverable modem library error, reboot to recover */
	slm_util_reboot(1);
}

static void exit_idle(struct k_work *work)
{
	int err;

	LOG_INF("Exit idle, full mode: %d", full_idle_mode);
	(void)gpio_pin_interrupt_configure(gpio_dev, CONFIG_SLM_INTERFACE_PIN,
				     GPIO_INT_DISABLE);
	gpio_remove_callback(gpio_dev, &gpio_cb);
	/* Do the same as nrf_gpio_cfg_default() */
	(void)gpio_pin_configure(gpio_dev, CONFIG_SLM_INTERFACE_PIN, GPIO_INPUT);

	if (full_idle_mode) {
		/* Restart SLM services */
		err = slm_at_host_init();
		if (err) {
			LOG_ERR("Failed to init at_host: %d", err);
		}
	} else {
		/* Power on UART only */
		err = poweron_uart(true);
		if (err) {
			LOG_ERR("Failed to wake up uart: %d", err);
		}
	}
}

static void gpio_callback(const struct device *dev,
		     struct gpio_callback *gpio_cb, uint32_t pins)
{
	k_work_submit_to_queue(&slm_work_q, &exit_idle_work);
}

void enter_idle(bool full_idle)
{
	int err;

	gpio_dev = device_get_binding(DT_LABEL(DT_NODELABEL(gpio0)));
	if (gpio_dev == NULL) {
		LOG_ERR("GPIO_0 bind error");
		return;
	}
	err = gpio_pin_configure(gpio_dev, CONFIG_SLM_INTERFACE_PIN,
				GPIO_INPUT | GPIO_PULL_UP);
	if (err) {
		LOG_ERR("GPIO_0 config error: %d", err);
		return;
	}
	gpio_init_callback(&gpio_cb, gpio_callback,
			BIT(CONFIG_SLM_INTERFACE_PIN));
	err = gpio_add_callback(gpio_dev, &gpio_cb);
	if (err) {
		LOG_ERR("GPIO_0 add callback error: %d", err);
		return;
	}
	err = gpio_pin_interrupt_configure(gpio_dev, CONFIG_SLM_INTERFACE_PIN,
					   GPIO_INT_LEVEL_LOW);
	if (err) {
		LOG_ERR("GPIO_0 enable callback error: %d", err);
		return;
	}

	full_idle_mode = full_idle;
}

void enter_sleep(void)
{
	//nrf_modem_lib_shutdown();

	/*
	 * Due to errata 4, Always configure PIN_CNF[n].INPUT before PIN_CNF[n].SENSE.
	 */
	nrf_gpio_cfg_input(CONFIG_SLM_INTERFACE_PIN,
		NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_sense_set(CONFIG_SLM_INTERFACE_PIN,
		NRF_GPIO_PIN_SENSE_LOW);

	nrf_regulators_system_off(NRF_REGULATORS_NS);
}

void enter_dfu(void)
{
	//nrf_modem_lib_shutdown();

	nrf_power_gpregret_set(NRF_POWER_NS, SLM_DFU_STATUS);
	slm_util_reboot(0);
}

void handle_nrf_modem_lib_init_ret(int ret)
{
	/* Handle return values relating to modem firmware update */
	switch (ret) {
	case 0:
		return; /* Initialization successful, no action required. */
	case MODEM_DFU_RESULT_OK:
		LOG_INF("MODEM UPDATE OK. Will run new firmware");
		fota_stage = FOTA_STAGE_COMPLETE;
		fota_status = FOTA_STATUS_OK;
		fota_info = 0;
		break;
	case MODEM_DFU_RESULT_UUID_ERROR:
	case MODEM_DFU_RESULT_AUTH_ERROR:
		LOG_ERR("MODEM UPDATE ERROR %d. Will run old firmware", ret);
		fota_status = FOTA_STATUS_ERROR;
		fota_info = ret;
		break;
	case MODEM_DFU_RESULT_HARDWARE_ERROR:
	case MODEM_DFU_RESULT_INTERNAL_ERROR:
		LOG_ERR("MODEM UPDATE FATAL ERROR %d. Modem failiure", ret);
		fota_status = FOTA_STATUS_ERROR;
		fota_info = ret;
		break;
	default:
		/* All non-zero return codes other than DFU result codes are
		 * considered irrecoverable and a reboot is needed.
		 */
		LOG_ERR("nRF modem lib initialization failed, error: %d", ret);
		fota_status = FOTA_STATUS_ERROR;
		fota_info = ret;
		break;
	}

	slm_setting_fota_save();
	LOG_WRN("Rebooting...");

	slm_util_reboot(1);
}

void start_execute(void)
{
	int err;
#if defined(CONFIG_SLM_EXTERNAL_XTAL)
	struct onoff_manager *clk_mgr;
	struct onoff_client cli = {};
#endif

	LOG_INF("Serial LTE Modem");

#if defined(CONFIG_SLM_EXTERNAL_XTAL)
	/* request external XTAL for UART */
	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	sys_notify_init_spinwait(&cli.notify);
	err = onoff_request(clk_mgr, &cli);
	if (err) {
		LOG_ERR("Clock request failed: %d", err);
		return;
	}
#endif

	/* Init and load settings */
	err = slm_settings_init();
	if (err) {
		LOG_ERR("Failed to init slm settings: %d", err);
		return;
	}

	/* Modem Post-FOTA handling */
	err = nrf_modem_lib_init(NORMAL_MODE);
	handle_nrf_modem_lib_init_ret(err);

	/* Init at_cmd and at_notify lib */
	err = at_cmd_init();
	if (err) {
		LOG_ERR("Failed to init at cmd: %d", err);
		return;
	}
	err = at_notif_init();
	if (err) {
		LOG_ERR("Failed to init at notify: %d", err);
		return;
	}

#if defined(CONFIG_NRF_MODEM_LIB_TRACE_ENABLED)
	/* Enable modem trace */
	static const char mdm_trace[] = "AT%XMODEMTRACE=1,2";

	if (at_cmd_write(mdm_trace, NULL, 0, NULL) != 0) {
		LOG_ERR("Failed to enable modem trace");
	}

#if defined(CONFIG_NRF_MODEM_LIB_TRACE_MEDIUM_RTT) && defined(CONFIG_NRF_MODEM_LIB_TRACE_RTT_DIAGNOSTICS)
	/* Print initial RTT trace buffer status */
	nrf_modem_lib_trace_rtt_diagnose();
#endif
#endif

	if (fota_type == DFU_TARGET_IMAGE_TYPE_MCUBOOT) {
		/* All initializations were successful mark image as working so that we
		 * will not revert upon reboot.
		 */
		err = boot_write_img_confirmed();
		if (fota_stage != FOTA_STAGE_INIT) {
			if (err) {
				fota_status = FOTA_STATUS_ERROR;
				fota_info = err;
			} else {
				fota_stage = FOTA_STAGE_COMPLETE;
				fota_status = FOTA_STATUS_OK;
				fota_info = 0;
			}
		}
	}

	err = slm_at_host_init();
	if (err) {
		LOG_ERR("Failed to init at_host: %d", err);
		return;
	}

	k_work_queue_start(&slm_work_q, slm_wq_stack_area,
			   K_THREAD_STACK_SIZEOF(slm_wq_stack_area),
			   SLM_WQ_PRIORITY, NULL);
	k_work_init(&exit_idle_work, exit_idle);
}

void start_dfu_execute(void)
{
	int err;

	/* Initialize modem in DFU mode */
	err = nrf_modem_lib_init(FULL_DFU_MODE);
	if (err) {
		LOG_ERR("Error in modem lib init: %d", err);
		return;
	}

	/* Register SMP Communication stats */
	fmfu_mgmt_stat_init();
	/* Registers the OS management command handler group */
	os_mgmt_register_group();
	/* Registers the image management command handler group */
	img_mgmt_register_group();
	/* Initialize MCUMgr handlers for full modem update */
	err = fmfu_mgmt_init();
	if (err) {
		LOG_ERR("Error in fmfu init: %d", err);
		return;
	}
#if MCUMGR_CONFIRM_IMAGE
	/** Need to keep in DFU mode if mcumgr would test then confirm image,
	 *  Which means two Reset by mcumgr to boot back to SLM mode
	 */
	if (boot_is_img_confirmed()) {
		LOG_INF("Current image confirmed");
		nrf_power_gpregret_set(NRF_POWER_NS, 0x00);
	} else {
		LOG_INF("Current image not confirmed yet");
	}
#else
	/** Exit DFU mode directly if mcumgr would confirm image only,
	 *  Which means one final Reset by mcumgr to boot back to SLM mode
	 */
	nrf_power_gpregret_set(NRF_POWER_NS, 0x00);
#endif
	LOG_INF("Enter DFU mode");
}

#if defined(CONFIG_SLM_START_SLEEP)
int main(void)
{
	uint32_t rr = nrf_power_resetreas_get(NRF_POWER_NS);
	uint8_t gp = nrf_power_gpregret_get(NRF_POWER_NS);

	LOG_DBG("RR: 0x%08x", rr);
	if (rr & NRF_POWER_RESETREAS_OFF_MASK) {
		nrf_power_resetreas_clear(NRF_POWER_NS, 0x70017);
		if (gp == SLM_DFU_STATUS) {
			nrf_uarte_disable(NRF_UARTE2);
			start_dfu_execute();
		} else {
			nrf_uarte_disable(NRF_UARTE0);
			nrf_uarte_enable(NRF_UARTE2);
			start_execute();
		}
	} else {
		LOG_INF("Sleep");
		enter_sleep();
	}

	return 0;
}
#else
int main(void)
{
#if defined(BUTTONLESS_DFU)
	uint8_t gp;

	gp = nrf_power_gpregret_get(NRF_POWER_NS);
	LOG_INF("GP: 0x%02x", gp);
	if (gp == SLM_DFU_STATUS) {
		nrf_uarte_disable(NRF_UARTE2);
		start_dfu_execute();
	} else {
		nrf_uarte_disable(NRF_UARTE0);
		nrf_uarte_enable(NRF_UARTE2);
		start_execute();
	}
#else
#define DFU_PIN 6
	int run_dfu;

	gpio_dev = device_get_binding(DT_LABEL(DT_NODELABEL(gpio0)));
	gpio_pin_configure(gpio_dev, DFU_PIN, GPIO_INPUT | GPIO_PULL_UP);
	k_sleep(K_MSEC(100));
	run_dfu = gpio_pin_get(gpio_dev, DFU_PIN);
	LOG_INF("run_slm: %d", run_dfu);
	if (run_dfu == 0) {
		nrf_uarte_disable(NRF_UARTE2);
		start_dfu_execute();
	} else {
		nrf_uarte_disable(NRF_UARTE0);
		nrf_uarte_enable(NRF_UARTE2);
		start_execute();
	}
#endif
	return 0;
}
#endif	/* CONFIG_SLM_GPIO_WAKEUP */
