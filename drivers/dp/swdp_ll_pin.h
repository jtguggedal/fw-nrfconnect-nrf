/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#if defined(CONFIG_SOC_SERIES_NRF52X)
#define CPU_CLOCK		64000000U
#else
#define CPU_CLOCK		CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
#endif

#if defined(CONFIG_SOC_SERIES_NRF52X) \
	|| defined(CONFIG_SOC_SERIES_NRF53X) \
	|| defined(CONFIG_SOC_FAMILY_LPC)
#define FAST_BITBANG_HW_SUPPORT 1
#else
#define FAST_BITBANG_HW_SUPPORT 0
#endif

static ALWAYS_INLINE void pin_delay_asm(uint32_t delay)
{
#if defined(CONFIG_CPU_CORTEX_M)
	__asm volatile ("movs r3, %[p]\n"
			".start_%=:\n"
			"subs r3, #1\n"
			"bne .start_%=\n"
			:
			: [p] "r" (delay)
			: "r3", "cc"
			);
#endif
}

#if FAST_BITBANG_HW_SUPPORT

static ALWAYS_INLINE void swdp_ll_pin_input(void *const base, uint32_t port, uint8_t pin)
{
#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF53X)
	NRF_GPIO_Type * reg = base;

	reg->PIN_CNF[pin] = 0b0000;
#elif defined(CONFIG_SOC_FAMILY_LPC)
	GPIO_Type *gpio_base = base;
	gpio_base->DIRCLR[port] = BIT(pin);
#endif
}

static ALWAYS_INLINE void swdp_ll_pin_output(void *const base, uint32_t port, uint8_t pin)
{
#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF53X)
	NRF_GPIO_Type * reg = base;

	reg->PIN_CNF[pin] = 0b0001;
#elif defined(CONFIG_SOC_FAMILY_LPC)
	GPIO_Type *gpio_base = base;

	gpio_base->DIRSET[port] = BIT(pin);
#endif
}


static ALWAYS_INLINE void swdp_ll_pin_set(void *const base, uint32_t port, uint8_t pin)
{
#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF53X)
	NRF_GPIO_Type * reg = base;

	reg->OUTSET = BIT(pin);
#elif defined(CONFIG_SOC_FAMILY_LPC)
	GPIO_Type *gpio_base = base;

	gpio_base->SET[port] = BIT(pin);
#endif
}

static ALWAYS_INLINE void swdp_ll_pin_clr(void *const base, uint32_t port, uint8_t pin)
{
#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF53X)
	NRF_GPIO_Type * reg = base;

	reg->OUTCLR = BIT(pin);
#elif defined(CONFIG_SOC_FAMILY_LPC)
	GPIO_Type *gpio_base = base;

	gpio_base->CLR[port] = BIT(pin);
#endif
}

static ALWAYS_INLINE uint32_t swdp_ll_pin_get(void *const base, uint32_t port, uint8_t pin)
{
#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF53X)
	NRF_GPIO_Type * reg = base;

	return ((reg->IN >> pin) & 1);
#elif defined(CONFIG_SOC_FAMILY_LPC)
	GPIO_Type *gpio_base = base;

	return ((gpio_base->PIN[port] >> pin) & 1);
#endif
}

#endif /* FAST_BITBANG_HW_SUPPORT */
