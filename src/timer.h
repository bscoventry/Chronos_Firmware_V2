#ifndef TIMER_H
#define TIMER_H

#include <nrfx_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#define TIMER_INST_IDX 0
//This is the time between stim
#define DEFAULT_STIM_PERIOD 4000000
// This is the time between SPI transac on DAC1 and switching 1.03 off
#define DEFAULT_PULSE_WIDTH 1000000  // x1: Time after main event
// This is the time between switching 1.03 off and SPI transac on DAC2 (gap between biphasic phases)
#define SWITCH_PERIOD 10  // us; time after EVENT1. 10 us between the two biphasic pulses.

typedef struct {
    uint32_t event1_max;
    uint32_t event2_max;
    uint32_t event3_max;
    uint32_t event0_error;
    uint32_t event0_max;
    uint32_t myerror;
    uint32_t mycounter;
} error_data;

void timer_init(void);
void get_error_data(error_data *data);
nrfx_timer_t measurement_timer_init(void);
void update_stim_frequency(uint16_t frequency_hz);
void update_pulse_width(uint16_t pulse_width_us);

#if defined(CONFIG_BT) && !defined(CONFIG_SPI)
void stim_timer_request_stop_after_burst(void);
void timer_stimulation_enable(void);
bool timer_stimulation_is_enabled(void);
#endif

#if !defined(CONFIG_BT)
/** First pulse (DAC1): GPIO + SPI. Called from RTC handler at start of each period. */
void timer_do_event0(void);
/** Initialize TIMER00 for one-shot biphasic bursts (no continuous scheduling). */
void timer_burst_init(void);
/** Start one biphasic period: clear timer, set CC1/2/3, enable. Disables after COMPARE3. */
void timer_start_one_shot_biphasic(void);
#endif

#endif