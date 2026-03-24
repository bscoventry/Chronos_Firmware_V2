/*
 * TIMER-driven stimulation period: one interrupt per period from TIMER2,
 * then HFCLK + TIMER0 for the biphasic burst. TIMER2 is used only here;
 * TIMER0 = burst (timer.c), TIMER1 = measurement (timer.c).
 */
#include <nrfx_timer.h>
#include <hal/nrf_clock.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <stdio.h>
#include "rtc_stim.h"
#include "timer.h"
#include "config.h"

/* Use TIMER20 for period only; TIMER00 and TIMER10 are used in timer.c */
static nrfx_timer_t period_timer = NRFX_TIMER_INSTANCE(NRF_TIMER20);
static uint32_t period_ticks;

static void period_timer_handler(nrf_timer_event_t event_type, void *p_context)
{
	(void)p_context;
	if (event_type != NRF_TIMER_EVENT_COMPARE0) {
		return;
	}

	/* Ensure HFCLK is running for SPI and TIMER0 burst */
	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_HFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED);

	/* Start of pulse: same as timer COMPARE0 (GPIO + DAC1 SPI) */
	timer_do_event0();
	/* Run one biphasic period via TIMER0 (COMPARE1/2/3); timer disables itself after COMPARE3 */
	timer_start_one_shot_biphasic();
}

void rtc_stim_start_lfclk(void)
{
	printf("RTC_STIM: starting LFCLK\n");
	nrf_clock_lf_src_set(NRF_CLOCK_S, NRF_CLOCK_LFCLK_RC);
	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_LFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_LFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_LFCLKSTARTED);
	printf("RTC_STIM: LFCLK started\n");
}

void rtc_stim_init(uint16_t frequency_hz)
{
	if (frequency_hz == 0) {
		printf("RTC_STIM: init skipped (frequency=0)\n");
		return;
	}
	uint32_t period_us = 1000000u / frequency_hz;
	period_ticks = nrfx_timer_us_to_ticks(&period_timer, period_us);
	if (period_ticks == 0) {
		period_ticks = 1;
	}
	printf("RTC_STIM: init freq=%u Hz period=%lu us ticks=%lu\n",
	       (unsigned int)frequency_hz,
	       (unsigned long)period_us,
	       (unsigned long)period_ticks);

	uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(period_timer.p_reg);
	nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
	config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	/* On this platform, explicitly connect TIMER20 IRQ for nrfx handler. */
	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER20), IRQ_PRIO_LOWEST,
		    nrfx_timer_irq_handler, &period_timer, 0);

	nrfx_err_t err = nrfx_timer_init(&period_timer, &config, period_timer_handler);
	if (err != 0) {
		printf("RTC_STIM: nrfx_timer_init failed err=%d\n", (int)err);
		return;
	}
	printf("RTC_STIM: TIMER20 init OK (base=%lu Hz)\n", (unsigned long)base_frequency);
	nrfx_timer_clear(&period_timer);
	nrfx_timer_extended_compare(&period_timer, NRF_TIMER_CC_CHANNEL0, period_ticks,
				    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
	nrfx_timer_enable(&period_timer);
	printf("RTC_STIM: TIMER20 enabled\n");
}
