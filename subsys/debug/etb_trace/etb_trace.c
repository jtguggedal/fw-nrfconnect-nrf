/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <debug/etb_trace.h>
#include <nrfx_power.h>

#define LAR_OFFSET			0xFB0
#define LSR_OFFSET			0xFB4
#define CS_UNLOCK_KEY			0xC5ACCE55
#define CS_LOCK_KEY			0x00000000

#define ETB_BASE_ADDR			0xE0051000
#define ETB_RDP				(ETB_BASE_ADDR + 0x004)
#define ETB_STS				(ETB_BASE_ADDR + 0x00C)
#define ETB_RRD				(ETB_BASE_ADDR + 0x010)
#define ETB_RRP				(ETB_BASE_ADDR + 0x014)
#define ETB_RWP				(ETB_BASE_ADDR + 0x018)
#define ETB_TRG				(ETB_BASE_ADDR + 0x01C)
#define ETB_CTL				(ETB_BASE_ADDR + 0x020)
#define ETB_RWD				(ETB_BASE_ADDR + 0x024)
#define ETB_FFSR			(ETB_BASE_ADDR + 0x300)
#define ETB_FFCR			(ETB_BASE_ADDR + 0x304)
#define ETB_LAR				(ETB_BASE_ADDR + ETB_OFFSET)

#define ATB_1_BASE_ADDR			0xE005A000
#define ATB_1_CTL			(ATB_1_BASE_ADDR + 0x000)
#define ATB_1_PRIO			(ATB_1_BASE_ADDR + 0x004)

#define ATB_2_BASE_ADDR			0xE005B000
#define ATB_2_CTL			(ATB_2_BASE_ADDR + 0x000)
#define ATB_2_PRIO			(ATB_2_BASE_ADDR + 0x004)

#define ATB_REPLICATOR_BASE_ADDR	0xE0058000
#define ATB_REPLICATOR_IDFILTER0	(ATB_REPLICATOR_BASE_ADDR + 0x000)
#define ATB_REPLICATOR_IDFILTER1	(ATB_REPLICATOR_BASE_ADDR + 0x004)

#define ETM_BASE_ADDR			0xE0041000
#define ETM_TRCPRGCTLR			(ETM_BASE_ADDR + 0x004) /* Programming Control Register */
#define ETM_TRCSTATR			(ETM_BASE_ADDR + 0x00C) /* Status Register */
#define ETM_TRCCONFIGR			(ETM_BASE_ADDR + 0x010) /* Trace Configuration Register */
#define ETM_TRCCCCTLR			(ETM_BASE_ADDR + 0x038) /* Cycle Count Control Register */
#define ETM_TRCSTALLCTLR		(ETM_BASE_ADDR + 0x02C) /* Stall Control Register */
#define ETM_TRCTSCTLR			(ETM_BASE_ADDR + 0x030) /* Global Timestamp Control Register */
#define ETM_TRCTRACEIDR			(ETM_BASE_ADDR + 0x040) /* Trace ID Register */
#define ETM_TRCVICTLR			(ETM_BASE_ADDR + 0x080) /* ViewInst Main Control Register */
#define ETM_TRCEVENTCTL0R		(ETM_BASE_ADDR + 0x020) /* Event Control 0 Register */
#define ETM_TRCEVENTCTL1R		(ETM_BASE_ADDR + 0x024) /* Event Control 1 Register */
#define ETM_TRCPDSR			(ETM_BASE_ADDR + 0x314) /* Power down status register */

#define ITM_BASE_ADDR			0xE0000000
#define ITM_TER				(ITM_BASE_ADDR + 0xE00) /* Trace Enable Register */
#define ITM_TCR				(ITM_BASE_ADDR + 0xE80) /* Trace Control Register */

#define DWT_BASE_ADDR                  	0xE0001000
#define DWT_CCYCCNT			(DWT_BASE_ADDR + 0x004) /* Cycle Count Register */

#define TIMESTAMP_GENERATOR_BASE_ADDR	0xE0053000
#define TIMESTAMP_GENERATOR_CNCTR	(TIMESTAMP_GENERATOR_BASE_ADDR + 0x000)

#define SET_REG(reg, value)	*((volatile uint32_t *)(reg)) = value
#define GET_REG(reg)		*((volatile uint32_t *)(reg))
#define CS_UNLOCK(reg_base)	*((volatile uint32_t *)(reg_base + LAR_OFFSET)) = CS_UNLOCK_KEY
#define CS_LOCK(reg_base)	*((volatile uint32_t *)(reg_base + LAR_OFFSET)) = CS_LOCK_KEY

/* Declaring as static variables to make them appear with the correct value in ELF file
 * for simplified trace processing.
 */

/* Bit 3: Branch broadcast mode enabled. */
static volatile uint32_t etm_trcconfigr = BIT(3);
/* Set trace ID to 0x10  */
static volatile uint32_t etm_trctraceidr = 0x10;

static void sleep_handler(nrfx_power_sleep_evt_t event) {
	if (event == NRFX_POWER_SLEEP_EVT_ENTER) {
		NRF_TAD_S->ENABLE = 0;
	} else {
		NRF_TAD_S->ENABLE = TAD_ENABLE_ENABLE_Msk;
	}
}

static nrfx_power_sleepevt_config_t sleepevt_config = {
	.en_enter = true,
	.en_exit = true,
	.handler = sleep_handler,
};

static void etm_init(void)
{
	/* Disable ETM to allow configuration */
	SET_REG(ETM_TRCPRGCTLR, 0);

	/* Wait until ETM is idle and PM is stable */
	while ((GET_REG(ETM_TRCSTATR) & (BIT(1) | BIT(0))) != (BIT(1) | BIT(0)));

	/* Configure the ETM */
	SET_REG(ETM_TRCCONFIGR, etm_trcconfigr);

	/* The trace unit can not stall the processor for instruction traces, at the risk of
	 * losing traces.
	 */
	SET_REG(ETM_TRCSTALLCTLR, 0);

	/* Global Timestamp Control Register to zero, no stamps included in the trace.  */
	SET_REG(ETM_TRCTSCTLR, 0);

	/* Set the trace stream ID */
	SET_REG(ETM_TRCTRACEIDR, etm_trctraceidr);

	/* Bit 0: Enable event 0.
	 * Bit 9: Indicates the current status of the start/stop logic.
	 * Bit 10: Always trace reset exceptions.
	 * Bit 11: Always trace system error exceptions.
	 */
	SET_REG(ETM_TRCVICTLR, BIT(11) | BIT(10) | BIT(9) | BIT(0));

	/* No events are configured */
	SET_REG(ETM_TRCEVENTCTL0R, 0);
	SET_REG(ETM_TRCEVENTCTL1R, 0);

	/* Enable ETM */
	SET_REG(ETM_TRCPRGCTLR, BIT(0));
}

static void etm_stop(void)
{
	/* Disable ETM */
	SET_REG(ETM_TRCPRGCTLR, 0);
}

static void itm_init(void)
{
	CS_UNLOCK(ITM_BASE_ADDR);

	/* Configure ITM */
	SET_REG(ITM_TCR, 0x0001000D);

	/* Enable ITM */
	SET_REG(ITM_TER, 0x1);

	CS_LOCK(ITM_BASE_ADDR);
}

static void itm_stop(void)
{
	CS_UNLOCK(ITM_BASE_ADDR);

	SET_REG(ITM_TER, 0x0);

	CS_LOCK(ITM_BASE_ADDR);
}

static void dwt_init(void)
{
	/* Set cycle count to zero */
	SET_REG(DWT_CCYCCNT, 0x0);
}

static void atb_init(void)
{
	/* ATB replicator */
	CS_UNLOCK(ATB_REPLICATOR_BASE_ADDR);

	/* ID filter for master port 0 */
	SET_REG(ATB_REPLICATOR_IDFILTER0, 0xFFFFFFFFUL);
	/* ID filter for master port 1, allowing only ETM traces from CM33 to ETB */
	SET_REG(ATB_REPLICATOR_IDFILTER1, 0xFFFFFFFDUL);

	CS_LOCK(ATB_REPLICATOR_BASE_ADDR);

	/* ATB funnel 1 */
	CS_UNLOCK(ATB_1_BASE_ADDR);

	/* Set pririty 1 for ports 0 and 1 */
	SET_REG(ATB_1_PRIO, 0x00000009UL);

	/* Enable port 0 and 1, and set hold time to 4 transactions */
	SET_REG(ATB_1_CTL, 0x00000303UL);

	CS_LOCK(ATB_1_BASE_ADDR);

	/* ATB funnel 2 */
	CS_UNLOCK(ATB_2_BASE_ADDR);

	/* Set priority 3 for port 3 */
	SET_REG(ATB_2_PRIO, 0x00003000UL);

	/* Enable ETM/ITM traces on port 3, and set hold time to 4 transactions */
	SET_REG(ATB_2_CTL,  0x00000308UL);

	CS_LOCK(ATB_2_BASE_ADDR);
}

static void etb_init(void)
{
	CS_UNLOCK(ETB_BASE_ADDR);

	/* Disable ETB */
	SET_REG(ETB_CTL, 0);

	/* Wait for formatter to stop */
	while ((GET_REG(ETB_FFSR) & BIT(1)) == 0);

	/* Enable formatter in continuous mode */
	SET_REG(ETB_FFCR, BIT(1) | BIT(0));

	/* Reset write pointer */
	SET_REG(ETB_RWP, 0);

	/* Clear memory */
	for (size_t i = 0; i < 512; i++) {
		SET_REG(ETB_RWD, 0);
	}

	/* Reset write pointer */
	SET_REG(ETB_RWP, 0);

	/* Enable ETB */
	SET_REG(ETB_CTL, 0x1);

	while ((GET_REG(ETB_FFSR) & BIT(1)) != 0);

	CS_LOCK(ETB_BASE_ADDR);
}

static void etb_stop(void)
{
	CS_UNLOCK(ETB_BASE_ADDR);

	/* Disable ETB */
	SET_REG(ETB_CTL, 0);

	/* Wait for formatter to stop */
	while (GET_REG(ETB_FFSR) & BIT(0));

	CS_LOCK(ETB_BASE_ADDR);
}

static void timestamp_generator_init(void)
{
	SET_REG(TIMESTAMP_GENERATOR_CNCTR, BIT(0));
}

static void debug_init(void)
{
	NRF_TAD_S->ENABLE = TAD_ENABLE_ENABLE_Msk;
	NRF_TAD_S->TASKS_CLOCKSTART = TAD_TASKS_CLOCKSTART_TASKS_CLOCKSTART_Msk;
}

void etb_trace_start(void)
{
	debug_init();
	atb_init();
	etb_init();
	timestamp_generator_init();
	etm_init();
	itm_init();
	dwt_init();
}

void etb_trace_stop(void)
{
	itm_stop();
	etm_stop();
	etb_stop();
	nrfx_power_sleepevt_disable();
}

size_t etb_data_get(volatile uint32_t *buf, size_t buf_size)
{
	size_t i = 0;

	if (buf_size == 0) {
		return 0;
	}

	CS_UNLOCK(ETB_BASE_ADDR);

	/* Set read pointer to the last write pointer */
	SET_REG(ETB_RRP, GET_REG(ETB_RWP));

	for (; i < buf_size; i++) {
		buf[i] = GET_REG(ETB_RRD);
	}

	return i + 1;
}

#if defined (CONFIG_ETB_TRACE_SYS_INIT)
static int init(const struct device *dev)
{
	ARG_UNUSED(dev);

	etb_trace_start();

	return 0;
}


static int init_power_saving(const struct device *dev)
{
	ARG_UNUSED(dev);

	nrfx_power_sleepevt_init(&sleepevt_config);
	nrfx_power_sleepevt_enable(&sleepevt_config);
	return 0;
}

SYS_INIT(init, EARLY, 0);
SYS_INIT(init_power_saving, POST_KERNEL, 0);
#endif /* defined (CONFIG_ETB_TRACE_SYS_INIT) */
