/*
 * Low-power stimulation period: TIMER20 fires once per stim period, then
 * runs one biphasic burst. CONFIG_BT=y: stim_run_one_burst() (SPI + k_busy_wait).
 * CONFIG_BT=n: TIMER00 one-shot (timer_do_event0 + timer_start_one_shot_biphasic).
 */
#include <zephyr/kernel.h>
#include <nrfx_timer.h>
#include <hal/nrf_clock.h>
#include <zephyr/irq.h>
#include <stdio.h>
#include <stdbool.h>

#include "rtc_stim.h"
#include "timer.h"

#if !defined(CONFIG_SPI)

/* 1 MHz tick rate: matches nrfx_timer_us_to_ticks(); lower prescaler activity than 16 MHz. */
#define PERIOD_TIMER_TICK_HZ  1000000u

/* TIMER20 = period; TIMER00 = burst phases (timer.c) */
static nrfx_timer_t period_timer = NRFX_TIMER_INSTANCE(NRF_TIMER20);
static uint32_t period_ticks;
static bool period_timer_inited;

static void period_timer_handler(nrf_timer_event_t event_type, void *p_context)
{
	(void)p_context;
	if (event_type != NRF_TIMER_EVENT_COMPARE0) {
		return;
	}

	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_HFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED);

#if defined(CONFIG_BT)
	stim_run_one_burst();
#else
	timer_do_event0();
	timer_start_one_shot_biphasic();
#endif
}

#endif /* !CONFIG_SPI */

void rtc_stim_start_lfclk(void)
{
#if defined(CONFIG_SPI)
	return;
#else
	static bool lfclk_started;

	if (lfclk_started) {
		return;
	}
	lfclk_started = true;

	printf("RTC_STIM: starting LFCLK\n");
	nrf_clock_lf_src_set(NRF_CLOCK_S, NRF_CLOCK_LFCLK_RC);
	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_LFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_LFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_LFCLKSTARTED);
	printf("RTC_STIM: LFCLK started\n");
#endif
}

#if !defined(CONFIG_SPI)

static void period_configure_compare(void)
{
	nrfx_timer_extended_compare(&period_timer, NRF_TIMER_CC_CHANNEL0, period_ticks,
				    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
}

static bool period_timer_hw_init(void)
{
	if (period_timer_inited) {
		return true;
	}

	uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(period_timer.p_reg);
	nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(PERIOD_TIMER_TICK_HZ);

	config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER20), STIM_TIMER_IRQ_PRIO,
		    nrfx_timer_irq_handler, &period_timer, 0);

	nrfx_err_t err = nrfx_timer_init(&period_timer, &config, period_timer_handler);

	if (err != 0) {
		printf("RTC_STIM: nrfx_timer_init failed err=%d\n", (int)err);
		return false;
	}

	period_timer_inited = true;
	printf("RTC_STIM: TIMER20 init OK (base=%lu Hz tick=%u Hz)\n",
	       (unsigned long)base_frequency, (unsigned int)PERIOD_TIMER_TICK_HZ);
	return true;
}

/** Set period_ticks from Hz; call only after period_timer_hw_init(). */
static uint32_t period_timer_apply_frequency(uint16_t frequency_hz)
{
	uint32_t period_us = 1000000u / (uint32_t)frequency_hz;

	period_ticks = nrfx_timer_us_to_ticks(&period_timer, period_us);
	if (period_ticks == 0U) {
		period_ticks = 1U;
	}

	return period_us;
}

void rtc_stim_stop(void)
{
	if (!period_timer_inited) {
		return;
	}
	nrfx_timer_disable(&period_timer);
}

bool rtc_stim_is_running(void)
{
	return period_timer_inited && nrfx_timer_is_enabled(&period_timer);
}

void rtc_stim_set_frequency(uint16_t frequency_hz)
{
	if (!period_timer_inited || frequency_hz == 0U) {
		return;
	}

	uint32_t period_us = period_timer_apply_frequency(frequency_hz);
	bool was_running = nrfx_timer_is_enabled(&period_timer);

	printf("RTC_STIM: set freq=%u Hz period=%lu us ticks=%lu\n",
	       (unsigned int)frequency_hz,
	       (unsigned long)period_us,
	       (unsigned long)period_ticks);

	nrfx_timer_disable(&period_timer);
	nrfx_timer_clear(&period_timer);
	period_configure_compare();
	if (was_running) {
		nrfx_timer_enable(&period_timer);
	}
}

void rtc_stim_init(uint16_t frequency_hz)
{
	if (frequency_hz == 0U) {
		printf("RTC_STIM: init skipped (frequency=0)\n");
		return;
	}

	if (!period_timer_hw_init()) {
		return;
	}

	uint32_t period_us = period_timer_apply_frequency(frequency_hz);

	printf("RTC_STIM: init freq=%u Hz period=%lu us ticks=%lu\n",
	       (unsigned int)frequency_hz,
	       (unsigned long)period_us,
	       (unsigned long)period_ticks);

	nrfx_timer_disable(&period_timer);
	nrfx_timer_clear(&period_timer);
	period_configure_compare();
	nrfx_timer_enable(&period_timer);
	printf("RTC_STIM: TIMER20 enabled\n");
}

#else /* CONFIG_SPI */

void rtc_stim_stop(void)
{
}

bool rtc_stim_is_running(void)
{
	return false;
}

void rtc_stim_set_frequency(uint16_t frequency_hz)
{
	ARG_UNUSED(frequency_hz);
}

void rtc_stim_init(uint16_t frequency_hz)
{
	ARG_UNUSED(frequency_hz);
}

#endif /* CONFIG_SPI */
