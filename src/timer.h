#ifndef TIMER_H
#define TIMER_H

#include <nrfx_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "config.h"

#define TIMER_INST_IDX 0
//This is the time between stim
#define DEFAULT_STIM_PERIOD 4000000
// This is the time between SPI transac on DAC1 and switching 1.03 off
#define DEFAULT_PULSE_WIDTH 1000000  // x1: Time after main event
/* Interphase gap (P1.02); single source in config.h */
#define SWITCH_PERIOD  CONFIG_INTER_PHASE_GAP_US

/* Stim timer IRQ: above LOWEST to reduce GPIO/DAC edge jitter under BLE. */
#define STIM_TIMER_IRQ_PRIO  2

/* TIMER00 burst phases: 1 MHz ticks so nrfx_timer_us_to_ticks matches real time. */
#define BURST_TIMER_TICK_HZ  1000000u

/* Analog switch GPIO (port 1); QFN52 exposes P1.00–P1.16 */
#define SWITCH_PIN_FIRST   16  /* P1.16 — ganged with P1.03 for both stimulus phases */
#define SWITCH_PIN_INTER   2   /* P1.02 — interphase only */
#define SWITCH_PIN_SECOND  3   /* P1.03 — ganged with P1.16 for both stimulus phases */

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
void timer_stimulation_enable_from_boot(void);
bool timer_stimulation_is_enabled(void);
/** BLE: one biphasic burst (SPI + GPIO timing) from TIMER20 period ISR. */
void stim_run_one_burst(void);
#endif

#if !defined(CONFIG_SPI)
/** First pulse (DAC1): GPIO + SPI. Called from TIMER20 period ISR at start of each period. */
void timer_do_event0(void);
/** Start one biphasic period: clear TIMER00, set CC1/2/3, enable. Disables after COMPARE3. */
void timer_start_one_shot_biphasic(void);
#if !defined(CONFIG_BT)
/** Initialize TIMER00 for one-shot biphasic bursts (no continuous scheduling). */
void timer_burst_init(void);
#endif
#endif

#endif