#include <nrfx_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_clock.h>
#include <inttypes.h>
#include <stdlib.h>
#include "timer.h"
#include "spi.h"
#include "config.h"

#if defined(CONFIG_BT) && !defined(CONFIG_SPI)
#include "rtc_stim.h"
#if defined(CONFIG_CHRONOS_STIM_NVM)
#include "stim_nvm.h"
#endif

static uint16_t lp_stim_freq_hz = (uint16_t)CONFIG_STIM_FREQUENCY_HZ;
#endif

extern int spi_ok;

/* When CONFIG_SPI is enabled (Zephyr SPI test build), we don't need the
 * nrfx timer-based stimulation engine. Provide lightweight stubs to avoid
 * pulling in nrfx_timer symbols that may not be linked in that config.
 */
#if defined(CONFIG_SPI)

nrfx_timer_t measurement_timer_init(void)
{
    nrfx_timer_t dummy = { 0 };
    return dummy;
}

void get_error_data(error_data *data)
{
    if (!data) {
        return;
    }
    data->event1_max = 0;
    data->event2_max = 0;
    data->event3_max = 0;
    data->event0_error = 0;
    data->event0_max = 0;
    data->myerror = 0;
    data->mycounter = 0;
}

void update_stim_frequency(uint16_t frequency_hz)
{
    ARG_UNUSED(frequency_hz);
}

void update_pulse_width(uint16_t pulse_width_us)
{
    ARG_UNUSED(pulse_width_us);
}

void timer_init(void)
{
}

#if defined(CONFIG_BT)
void stim_timer_request_stop_after_burst(void) {}
void timer_stimulation_enable(void) {}
void timer_stimulation_enable_from_boot(void) {}
bool timer_stimulation_is_enabled(void)
{
	return false;
}
#endif

void timer_do_event0(void) {}
void timer_start_one_shot_biphasic(void) {}

#else /* !CONFIG_SPI: full nrfx timer implementation */

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
/* Re-apply P1.02/P1.03/P1.16 MCUSEL to App before each use; network/pinctrl can overwrite. */
static void p1_mcu_select_app_sync(void)
{
#if defined(NRF_P1)
	NRF_GPIO_Type *p1 = NRF_P1;
#elif defined(NRF_GPIO1)
	NRF_GPIO_Type *p1 = NRF_GPIO1;
#else
#error "Need NRF_P1 or NRF_GPIO1 for P1 MCUSEL"
#endif
	uint32_t msk = 3u << 16;
	uint32_t app = 1u << 16;
	uint32_t c2 = p1->PIN_CNF[2] & ~msk;
	uint32_t c3 = p1->PIN_CNF[3] & ~msk;
	uint32_t c16 = p1->PIN_CNF[16] & ~msk;
	p1->PIN_CNF[2] = c2 | app;
	p1->PIN_CNF[3] = c3 | app;
	p1->PIN_CNF[16] = c16 | app;
}
#endif

static const uint8_t dac_neutral_tx[DAC_TX_LEN] = { 0x80, 0x00 };

/* Howland bipolar: P1.16 + P1.03 ganged for both phases; polarity from DAC only. */
static void switches_pulse_on(void)
{
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_INTER));
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_FIRST));
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_SECOND));
}

static void switches_interphase(void)
{
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_FIRST));
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_INTER));
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_SECOND));
}

static void switches_all_off(void)
{
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_FIRST));
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_INTER));
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, SWITCH_PIN_SECOND));
}

static void dac_write_buf(const uint8_t *tx)
{
	if (spi_ok) {
		spi_write_dac1((uint8_t *)tx, dac1_buf_rx);
	}
}

/* Close pulse path at neutral, allow network to settle, then update DAC (path stays on). */
static void switch_settle_then_dac(const uint8_t *tx)
{
	switches_pulse_on();
	if (CONFIG_DAC_SETTLE_US > 0U) {
		k_busy_wait(CONFIG_DAC_SETTLE_US);
	}
	dac_write_buf(tx);
}

static void stim_phase1_begin(void)
{
	switch_settle_then_dac(dac2_buf_tx);
}

static void stim_interphase_begin(void)
{
	/* Open stimulus path before neutral DAC (avoids discharge through closed switch). */
	switches_interphase();
	dac_write_buf(dac_neutral_tx);
}

static void stim_interphase_end(void)
{
	switches_all_off();
}

static void stim_phase2_begin(void)
{
	switch_settle_then_dac(dac1_buf_tx);
}

static void stim_idle_begin(void)
{
	/* Open path before neutral DAC (avoids 0 A step through closed electrode path). */
	switches_all_off();
	if (CONFIG_DAC_SETTLE_US > 0U) {
		k_busy_wait(CONFIG_DAC_SETTLE_US);
	}
	dac_write_buf(dac_neutral_tx);
}

static uint32_t timer_freq_hz = 0;  
static uint32_t main_event_time = 0;
static uint32_t event1_time __attribute__((unused)) = 0;
static uint32_t event2_time __attribute__((unused)) = 0;
static uint32_t event3_time __attribute__((unused)) = 0;

static atomic_t counter;            // test variable to record how many times the timer handler has been called 
static atomic_t error;
static atomic_t event1_error_max;
static atomic_t event2_error_max;
static atomic_t event3_error_max;
static atomic_t event0_error_counter;
static atomic_t event0_error_max;
static uint32_t prev_main_event_time = 0;
#if defined(CONFIG_SOC_NRF54L15)
static nrfx_timer_t measurement_timer = NRFX_TIMER_INSTANCE(NRF_TIMER10);
#else
static nrfx_timer_t measurement_timer = NRFX_TIMER_INSTANCE(NRF_TIMER_INST_GET(1));
#endif
/* nrfx expects peripheral base, not index. NRF_TIMER00 = app core timer on nRF54L15. */
#if defined(CONFIG_SOC_NRF54L15)
static nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(NRF_TIMER00);
#else
static nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(NRF_TIMER_INST_GET(TIMER_INST_IDX));
#endif
static uint32_t current_period_us = DEFAULT_STIM_PERIOD;
static uint32_t current_pulse_width_us = CONFIG_PULSE_WIDTH_US;
static void timer_handler(nrf_timer_event_t event_type, void * p_context);

static uint32_t burst_cc2_ticks;
static uint32_t burst_cc3_ticks;
static bool burst_phase2_done;
static bool burst_idle_done;

/* Set TIMER00 CC1/2/3 from pulse width (us); call only after nrfx_timer_init(). */
static void burst_set_compare_ticks(uint32_t pulse_width_us)
{
	uint32_t e1 = nrfx_timer_us_to_ticks(&timer_inst, pulse_width_us);
	uint32_t e2 = nrfx_timer_us_to_ticks(&timer_inst, pulse_width_us + SWITCH_PERIOD);
	uint32_t e3 = nrfx_timer_us_to_ticks(&timer_inst,
					      2 * pulse_width_us + SWITCH_PERIOD);

	burst_cc2_ticks = e2;
	burst_cc3_ticks = e3;

	nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL1, e1, true);
	nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL2, e2, true);
	nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL3, e3, true);
}

/* If SPI blocked past CC2/CC3, run missed phases before compares are lost. */
static void burst_catchup_after_delay(void)
{
	uint32_t cnt = nrfx_timer_capture(&timer_inst, NRF_TIMER_CC_CHANNEL4);

	if (cnt >= burst_cc3_ticks) {
		if (!burst_phase2_done) {
			stim_phase2_begin();
			burst_phase2_done = true;
		}
		if (!burst_idle_done) {
			stim_idle_begin();
			burst_idle_done = true;
		}
	} else if (cnt >= burst_cc2_ticks) {
		if (!burst_phase2_done) {
			stim_phase2_begin();
			burst_phase2_done = true;
		}
	}
}

#if defined(CONFIG_BT)
static atomic_t stim_stop_pending;

/* BLE stack may allow HFCLK to gate off between events; SPIM + TIMER00 need it during stim. */
static void stim_ensure_hfclk(void)
{
	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_HFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED);
}
#endif

void get_error_data(error_data *data) {
    data->event1_max = atomic_get(&event1_error_max);
    data->event2_max = atomic_get(&event2_error_max);
    data->event3_max = atomic_get(&event3_error_max);
    data->event0_error = atomic_get(&event0_error_counter);
    data->event0_max = atomic_get(&event0_error_max);
    data->myerror = atomic_get(&error);
    data->mycounter = atomic_get(&counter);
}

void update_stim_frequency(uint16_t frequency_hz) {
    if (frequency_hz == 0) {
        printf("Invalid frequency: 0 Hz\n");
        return;
    }
    
    // Calculate period in microseconds from frequency in Hz
    uint32_t period_us = 1000000 / frequency_hz;
    current_period_us = period_us;

#if defined(CONFIG_BT) && !defined(CONFIG_SPI)
    lp_stim_freq_hz = frequency_hz;
    if (rtc_stim_is_running()) {
        rtc_stim_set_frequency(frequency_hz);
    }

    if (MEASURE_TIMER == 1) {
        atomic_set(&event0_error_counter, 0);
        atomic_set(&event0_error_max, 0);
        atomic_set(&counter, 0);
        atomic_set(&error, 0);
        atomic_set(&event1_error_max, 0);
        atomic_set(&event2_error_max, 0);
        atomic_set(&event3_error_max, 0);
    }

    printf("Timer frequency updated to %u Hz (period: %" PRIu32 " us; LP TIMER20)\n",
           frequency_hz, period_us);
    return;
#endif

    // Convert to timer ticks (continuous TIMER00 scheduling when CONFIG_BT=n)
    uint32_t period_ticks = nrfx_timer_us_to_ticks(&timer_inst, period_us);

    //LEE ADDING CODE *************************************************************************************************************************
    //Clear the TIMER to stop missing compare events
    nrfx_timer_disable(&timer_inst);
    nrfx_timer_clear(&timer_inst);
    //LEE DONE ADDING CODE*************************************************************************************************************************

    // Update channel 0 compare value
    // Note: We keep the SHORT to clear on compare to maintain periodic operation
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0, period_ticks, 
        NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

    //LEE STILL ADDING CODE*************************************************************************************************************************
    nrfx_timer_enable(&timer_inst);
    //LEE DONE ADDING CODE*************************************************************************************************************************

    // Also update the measurement timer expectations if needed
    if (MEASURE_TIMER == 1) {
        // Reset error counters when frequency changes
        atomic_set(&event0_error_counter, 0);
        atomic_set(&event0_error_max, 0);
        atomic_set(&counter,0);            
        atomic_set(&error,0);
        atomic_set(&event1_error_max,0);
        atomic_set(&event2_error_max,0);
        atomic_set(&event3_error_max,0);
    }
    
    printf("Timer frequency updated to %u Hz (period: %" PRIu32 " us, ticks: %" PRIu32 ")\n",
           frequency_hz, period_us, period_ticks);
}

void update_pulse_width(uint16_t pulse_width_us) {
    if (pulse_width_us == 0) {
        printf("Invalid pulse width: 0 us\n");
        return;
    }
    
    current_pulse_width_us = pulse_width_us;

    burst_set_compare_ticks(pulse_width_us);

    uint32_t channel1_ticks = nrfx_timer_us_to_ticks(&timer_inst, pulse_width_us);
    uint32_t channel2_us = pulse_width_us + SWITCH_PERIOD;
    uint32_t channel3_us = channel2_us + pulse_width_us;

    printf("Pulse width updated to %u us (ticks: %" PRIu32 ")\n", pulse_width_us, channel1_ticks);
    printf("Channel 1 at %u us, Channel 2 at %" PRIu32 " us, Channel 3 at %" PRIu32 " us\n",
           pulse_width_us, channel2_us, channel3_us);
}

void timer_init(void)
{
    atomic_set(&counter, 0);
    atomic_set(&error, 0);
    uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_inst.p_reg);
    timer_freq_hz = base_frequency;

    /* Connect TIMER00 interrupt to nrfx timer handler (Zephyr does not do this
     * automatically for nrfx drivers).
     */
    IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER00), STIM_TIMER_IRQ_PRIO,
                nrfx_timer_irq_handler, &timer_inst, 0);

    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(BURST_TIMER_TICK_HZ);
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    config.p_context = &timer_inst;
    nrfx_err_t status = nrfx_timer_init(&timer_inst, &config, timer_handler);
    nrfx_timer_clear(&timer_inst);
    if (status != 0) {
        printf("Timer initialization failed with error: %d\n", status);
        return;
    }

    printf("TIMER00 init OK (base=%" PRIu32 " Hz tick=%u Hz)\n",
           base_frequency, (unsigned int)BURST_TIMER_TICK_HZ);

    /* CONFIG_BT=y: TIMER00 only for one-shot biphasic burst (CC1/2/3). Period = TIMER20 (rtc_stim).
     * CONFIG_BT=n: continuous TIMER00 + CC0 periodic + CC1/2/3.
     */
    current_pulse_width_us = CONFIG_PULSE_WIDTH_US;
    burst_set_compare_ticks(CONFIG_PULSE_WIDTH_US);
#if defined(CONFIG_BT)
    nrfx_timer_disable(&timer_inst);
    printf("Timer status: init OK, stimulation disabled until START (LP TIMER20)\n");
#else
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0,
        nrfx_timer_us_to_ticks(&timer_inst, 1000000u / CONFIG_STIM_FREQUENCY_HZ),
        NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    nrfx_timer_enable(&timer_inst);
    printf("Timer status: %s (continuous TIMER00)\n",
           nrfx_timer_is_enabled(&timer_inst) ? "enabled" : "disabled");
#endif
}

#if defined(CONFIG_BT)
void stim_timer_request_stop_after_burst(void)
{
	atomic_set(&stim_stop_pending, 1);
}

void timer_stimulation_enable_from_boot(void)
{
	atomic_set(&stim_stop_pending, 0);
	stim_ensure_hfclk();
	rtc_stim_start_lfclk();
	nrfx_timer_disable(&timer_inst);
	nrfx_timer_clear(&timer_inst);
	rtc_stim_init(lp_stim_freq_hz);
}

void timer_stimulation_enable(void)
{
#if defined(CONFIG_CHRONOS_STIM_NVM)
	if (stim_nvm_ble_stim_blocked()) {
		printf("Stimulation blocked: NVM missing or CRC bad; send START after BLE connects.\n");
		return;
	}
#endif
	timer_stimulation_enable_from_boot();
}

bool timer_stimulation_is_enabled(void)
{
	return rtc_stim_is_running();
}

void stim_run_one_burst(void)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	p1_mcu_select_app_sync();
#endif
	stim_ensure_hfclk();

	stim_phase1_begin();
	k_busy_wait(current_pulse_width_us);
	stim_interphase_begin();
	k_busy_wait(SWITCH_PERIOD);
	stim_interphase_end();
	stim_phase2_begin();
	k_busy_wait(current_pulse_width_us);
	stim_idle_begin();

	if (atomic_get(&stim_stop_pending)) {
		atomic_set(&stim_stop_pending, 0);
		rtc_stim_stop();
	}
}
#endif

#if !defined(CONFIG_BT)
void timer_burst_init(void)
{
    atomic_set(&counter, 0);
    atomic_set(&error, 0);

    uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_inst.p_reg);

    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(BURST_TIMER_TICK_HZ);
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    config.p_context = &timer_inst;

    nrfx_err_t status = nrfx_timer_init(&timer_inst, &config, timer_handler);
    nrfx_timer_clear(&timer_inst);

    if (status != 0) {
        printf("Timer burst initialization failed: %d\n", (int)status);
        return;
    }

    printf("TIMER00 init OK (base=%" PRIu32 " Hz tick=%u Hz)\n",
           base_frequency, (unsigned int)BURST_TIMER_TICK_HZ);

    /* No compare values and no enable here.
     * timer_start_one_shot_biphasic() will set CC1/CC2/CC3 and enable. */
}
#endif

void timer_do_event0(void)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
    p1_mcu_select_app_sync();
#endif
    stim_phase1_begin();
}

void timer_start_one_shot_biphasic(void)
{
    nrfx_timer_disable(&timer_inst);
    nrfx_timer_clear(&timer_inst);
    burst_phase2_done = false;
    burst_idle_done = false;
    burst_set_compare_ticks(current_pulse_width_us);
    nrfx_timer_enable(&timer_inst);
}

nrfx_timer_t measurement_timer_init() {
    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(NRF_TIMER_BASE_FREQUENCY_GET(measurement_timer.p_reg));
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    nrfx_err_t err = nrfx_timer_init(&measurement_timer, &config, NULL); // No handler needed
    (void)err;
    nrfx_timer_enable(&measurement_timer);
    return measurement_timer;
}

static void timer_handler(nrf_timer_event_t event_type, void * p_context)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	p1_mcu_select_app_sync();
#endif
    // Get reference to timer
    atomic_inc(&counter);
    //printf("Time handler count: %i \n", counter);
    nrfx_timer_t *timer_inst = (nrfx_timer_t *)p_context;
    uint32_t current_time;
    uint32_t current_max;
    uint32_t my_error;
    uint32_t elapsed1_ticks;
    
    switch(event_type) {
        case NRF_TIMER_EVENT_COMPARE0:
#if defined(CONFIG_BT)
            stim_ensure_hfclk();
#endif
            if(MEASURE_TIMER == 1){
                current_time = nrfx_timer_capture(&measurement_timer, NRF_TIMER_CC_CHANNEL0);
                    
                if (prev_main_event_time > 0) {
                    // Calculate actual interval duration
                    uint32_t interval_ticks = current_time - prev_main_event_time;
                    uint32_t expected_ticks = nrfx_timer_us_to_ticks(&measurement_timer, DEFAULT_STIM_PERIOD);
                    int32_t diff = (int32_t)(interval_ticks - expected_ticks);
                    uint32_t event0_error = (uint32_t)abs(diff);
                    
                    // Update statistics
                    atomic_add(&event0_error_counter, event0_error);
                    
                    // Track maximum error
                    current_max = atomic_get(&event0_error_max);
                    if (event0_error > current_max) {
                        atomic_set(&event0_error_max, event0_error);
                    }
                }
                prev_main_event_time = current_time;
                // Capture timestamp when main event occurs (after timer reset)
                main_event_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
            }

            // Cathode phase: P1.16 + P1.03 at neutral, then DAC2
            stim_phase1_begin();
            break;
            
        case NRF_TIMER_EVENT_COMPARE1:
#if defined(CONFIG_BT)
            stim_ensure_hfclk();
#endif
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 1 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from main event
                elapsed1_ticks = current_time - main_event_time;
                int32_t diff1 = (int32_t)(elapsed1_ticks - DEFAULT_PULSE_WIDTH*timer_freq_hz/1000000);
                my_error = (uint32_t)abs(diff1);
                atomic_add(&error,my_error);
                current_max = atomic_get(&event1_error_max);
                if (my_error > current_max) {atomic_set(&event1_error_max, my_error);}
            }
        
            stim_interphase_begin();
            burst_catchup_after_delay();
            break;
            
        case NRF_TIMER_EVENT_COMPARE2:
#if defined(CONFIG_BT)
            stim_ensure_hfclk();
#endif
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 2 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from event 1
                int32_t diff2 = (int32_t)(elapsed1_ticks - SWITCH_PERIOD*timer_freq_hz/1000000);
                my_error = (uint32_t)abs(diff2);
                atomic_add(&error, my_error);
                current_max = atomic_get(&event2_error_max);
                if (my_error > current_max) {atomic_set(&event2_error_max, my_error);}
            }
            
            if (!burst_phase2_done) {
                stim_interphase_end();
                stim_phase2_begin();
                burst_phase2_done = true;
            }
            break;
            
        case NRF_TIMER_EVENT_COMPARE3:
#if defined(CONFIG_BT)
            stim_ensure_hfclk();
#endif
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 3 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from event 2
                int32_t diff3 = (int32_t)(elapsed1_ticks - DEFAULT_PULSE_WIDTH*timer_freq_hz/1000000);
                my_error = (uint32_t)abs(diff3);
                atomic_add(&error,my_error);
                current_max = atomic_get(&event3_error_max);
                if (my_error > current_max) {atomic_set(&event3_error_max, my_error);}
            }

            if (!burst_idle_done) {
                stim_idle_begin();
                burst_idle_done = true;
            }

#if defined(CONFIG_BT)
            if (atomic_get(&stim_stop_pending)) {
                nrfx_timer_disable(timer_inst);
                atomic_set(&stim_stop_pending, 0);
                rtc_stim_stop();
            }
#else
            /* End of biphasic burst (low-power path): stop TIMER00 until next RTC period. */
            nrfx_timer_disable(timer_inst);
#endif
            break;
        default:
            break;
    }
}

#endif /* !CONFIG_SPI */