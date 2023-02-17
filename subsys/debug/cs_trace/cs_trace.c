/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
#include <debug/cs_trace.h>

#define LAR_OFFSET			0xFB0
#define LSR_OFFSET			0xFB4
#define CS_UNLOCK_KEY			0xC5ACCE55
#define CS_LOCK_KEY			0x00000000

#define ETB_BASE			0xE0051000
#define ETB_RDP				(ETB_BASE + 0x004)
#define ETB_STS				(ETB_BASE + 0x00C)
#define ETB_RRD				(ETB_BASE + 0x010)
#define ETB_RRP				(ETB_BASE + 0x014)
#define ETB_RWP				(ETB_BASE + 0x018)
#define ETB_TRG				(ETB_BASE + 0x01C)
#define ETB_CTL				(ETB_BASE + 0x020)
#define ETB_RWD				(ETB_BASE + 0x024)
#define ETB_FFSR			(ETB_BASE + 0x300)
#define ETB_FFCR			(ETB_BASE + 0x304)
#define ETB_LAR				(ETB_BASE + ETB_OFFSET)

#define ATB_CM33_BASE			0xE005A000
#define ATB_CM33_CTL			(ATB_CM33_BASE + 0x000)
#define ATB_CM33_PRIO			(ATB_CM33_BASE + 0x004)

#define ATB_COMMON_BASE			0xE005B000
#define ATB_COMMON_CTL			(ATB_COMMON_BASE + 0x000)
#define ATB_COMMON_PRIO			(ATB_COMMON_BASE + 0x004)

#define ATB_REPLICATOR_BASE		0xE0058000
#define ATB_REPLICATOR_IDFILTER0	(ATB_REPLICATOR_BASE + 0x000)
#define ATB_REPLICATOR_IDFILTER1	(ATB_REPLICATOR_BASE + 0x004)

#define ETM_BASE			0xE0041000
#define ETM_TRCPRGCTLR			(ETM_BASE + 0x004) /* Programming Control Register */
#define ETM_TRCSTATR			(ETM_BASE + 0x00C) /* Status Register */
#define ETM_TRCCONFIGR			(ETM_BASE + 0x010) /* Trace Configuration Register */
#define ETM_TRCCCCTLR			(ETM_BASE + 0x038) /* Cycle Count Control Register */
#define ETM_TRCSTALLCTLR		(ETM_BASE + 0x02C) /* Stall Control Register */
#define ETM_TRCTSCTLR			(ETM_BASE + 0x030) /* Global Timestamp Control Register */
#define ETM_TRCTRACEIDR			(ETM_BASE + 0x040) /* Trace ID Register */
#define ETM_TRCVICTLR			(ETM_BASE + 0x080) /* ViewInst Main Control Register */
#define ETM_TRCEVENTCTL0R		(ETM_BASE + 0x020) /* Event Control 0 Register */
#define ETM_TRCEVENTCTL1R		(ETM_BASE + 0x024) /* Event Control 1 Register */
#define ETM_TRCPDSR			(ETM_BASE + 0x314) /* Power down status register */

#define TIMESTAMP_GENERATOR_BASE	0xE0053000
#define TIMESTAMP_GENERATOR_CNCTR	(TIMESTAMP_GENERATOR_BASE + 0x000)

#define SET_REG(reg, value)	*((volatile uint32_t *)(reg)) = value
#define GET_REG(reg)		*((volatile uint32_t *)(reg))
#define CS_UNLOCK(reg_base)	*((volatile uint32_t *)(reg_base + LAR_OFFSET)) = CS_UNLOCK_KEY
#define CS_LOCK(reg_base)	*((volatile uint32_t *)(reg_base + LAR_OFFSET)) = CS_LOCK_KEY

/* Keeping as a static variables to make them appear with the correct value in ELF file
 * for improved trace processing.
 */

/* Bit 3: Branch broadcast mode enabled.
 * Bit 4: Enable Cycle counting in instruction trace.
 * Bit 5-10 set to 0b0111: All conditional instructions are traced.
 * Bit 12: Return stack enabled.
 */
static volatile uint32_t etm_trcconfigr = BIT(12) | (7 << 5) | BIT(4) | BIT(3);
/* Set trace ID to 0x10  */
static volatile uint32_t etm_trctraceidr = 0x10;

void etm_init(void)
{
	SET_REG(0xE0041300, 0);

	/* Disable ETM to allow configuration */
	SET_REG(ETM_TRCPRGCTLR, 0);

	/* Wait until ETM is idle and PM is stable */
	while ((GET_REG(ETM_TRCSTATR) & (BIT(1) | BIT(0))) != (BIT(1) | BIT(0)));

	SET_REG(0xE0041008, 0);

	SET_REG(ETM_TRCCONFIGR, etm_trcconfigr);

	/* Set write cycle count to 1000, ie cycle count traces miniumum every 1000 trace  */
	SET_REG(ETM_TRCCCCTLR, 0x3E8);

	/* The trace unit can stall the processor for instruction traces */
	SET_REG(ETM_TRCSTALLCTLR, 0x00000108);
	/* Global Timestamp Control Register to zero  */
	SET_REG(ETM_TRCTSCTLR, 0);

	SET_REG(ETM_TRCTRACEIDR, etm_trctraceidr);

	SET_REG(ETM_TRCVICTLR, BIT(11) | (7 << 8) | BIT(0));

	SET_REG(0xE0041084, 0);
	SET_REG(0xE0041088, 0);

	SET_REG(ETM_TRCEVENTCTL0R, 0);
	SET_REG(ETM_TRCEVENTCTL1R, 0);

	/* Enable ETM */
	SET_REG(ETM_TRCPRGCTLR, BIT(0));
}

void etm_stop(void)
{
	SET_REG(0xE0041300, 0);
	/* Disable ETM */
	SET_REG(ETM_TRCPRGCTLR, 0);
}

void itm_init(void)
{
	ITM_Type *itm = ITM;

	CS_UNLOCK(ITM_BASE);

	/* Configure ITM */
	itm->TCR = 0x0001000D;

	/* Some secret sauce is written to stimulus registers */
	itm->PORT[0].u32 = 0x1;

	/* Enable ITM */
	itm->TER = 0x1;

	CS_LOCK(ITM_BASE);
}

void itm_stop(void)
{
	ITM_Type *itm = ITM;

	CS_UNLOCK(ITM_BASE);

	/* Disable ITM */
	itm->TER = 0x0;

	CS_LOCK(ITM_BASE);
}

void dwt_init(void)
{
	SET_REG(0xE0001004, 0xFFFFFFFF);
	SET_REG(0xE0001004, 0x00000000);
	SET_REG(0xE0001004, 0x0B5AB746);
	SET_REG(0xE0001004, 0x00FFFF00);
}

void atb_init(void)
{
	/* ATB replicator */
	CS_UNLOCK(ATB_REPLICATOR_BASE);

	/* ID filter for master port 0 */
	SET_REG(ATB_REPLICATOR_IDFILTER0, 0xFFFFFFFFUL);
	/* ID filter for master port 1, allowing only ETM traces from CM33 to ETB */
	SET_REG(ATB_REPLICATOR_IDFILTER1, 0xFFFFFFFDUL);

	CS_LOCK(ATB_REPLICATOR_BASE);


	/* ATB funnel - CM33 */
	CS_UNLOCK(ATB_CM33_BASE);

	/* Set pririty 1 for ports 0 and 1 */
	SET_REG(ATB_CM33_PRIO, 0x00000009UL);
	/* Enable port 0 and 1, and set hold time to 4 transactions */
	SET_REG(ATB_CM33_CTL, 0x00000303UL);

	CS_LOCK(ATB_CM33_BASE);


	/* ATB funnel - Common */
	CS_UNLOCK(ATB_COMMON_BASE);

	/* Set priority 3 for port 3 */
	SET_REG(ATB_COMMON_PRIO, 0x00003000UL);

	/* Enable Cortex-M33 ETM/ITM traces (port 3), and set hold time to 4 transactions */
	SET_REG(ATB_COMMON_CTL,  0x00000308UL);

	CS_LOCK(ATB_COMMON_BASE);
}

void etb_init(void)
{
	CS_UNLOCK(ETB_BASE);

	SET_REG(ETB_CTL, 0);

	while ((GET_REG(ETB_FFSR) & BIT(1)) == 0);

	/* Enable formatter in continuous mode */
	SET_REG(ETB_FFCR, BIT(1) | BIT(0));
	/* Reset write pointer */
	SET_REG(ETB_RWP, 0);

	/* Clear memory */

	for (size_t i = 0; i < 512; i++) {
		SET_REG(ETB_RWD, 0);
	}

	SET_REG(ETB_RWP, 0);


	/* Enabling ETB */
	SET_REG(ETB_CTL, 0x1);

	while ((GET_REG(ETB_FFSR) & BIT(1)) != 0);

	CS_LOCK(ETB_BASE);

}

void etb_stop(void)
{
	CS_UNLOCK(ETB_BASE);

	/* Disable ETB */
	SET_REG(ETB_CTL, 0);

	/*Wait for formatter to stop */
	while (GET_REG(ETB_FFSR) & BIT(0));

	CS_LOCK(ETB_BASE);

}

void timestamp_generator_init(void)
{
	SET_REG(TIMESTAMP_GENERATOR_CNCTR, BIT(0));
}

void trace_init(void)
{
	atb_init();
	etb_init();
	timestamp_generator_init();
	etm_init();
	itm_init(); /* Incomplete, something is written to stimulus registers */
	dwt_init();
}

size_t etb_data_get(uint32_t *buf, size_t buf_size)
{
	size_t i = 0;

	if (buf_size == 0) {
		return 0;
	}

	itm_stop();
	etm_stop();
	etb_stop();

	CS_UNLOCK(ETB_BASE);

	/* Set read pointer to the last write pointer */
	SET_REG(ETB_RRP, GET_REG(ETB_RWP));

	for (; i < buf_size; i++) {
		buf[i] = GET_REG(ETB_RRD);
	}

	return i + 1;
}

#if defined (CONFIG_CS_TRACE_SYS_INIT)
static int init(const struct device *dev)
{
	ARG_UNUSED(dev);

	trace_init();

	return 0;
}

SYS_INIT(init, EARLY, 0);
#endif /* defined (CONFIG_CS_TRACE_SYS_INIT) */

