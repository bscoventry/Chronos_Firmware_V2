#ifndef RTC_STIM_H
#define RTC_STIM_H

#include <stdint.h>

/** Start LFCLK (32.768 kHz). Optional; call before rtc_stim_init if needed. */
void rtc_stim_start_lfclk(void);

/**
 * Initialize stimulation period using TIMER2 (does not use TIMER0 or TIMER1).
 * Compare event fires every (1/frequency_hz) seconds. On each compare: HFCLK
 * is ensured, event0 (DAC1) runs, then TIMER0 one-shot runs for the biphasic phases.
 * @param frequency_hz Stimulation rate in Hz (e.g. 130).
 */
void rtc_stim_init(uint16_t frequency_hz);

#endif /* RTC_STIM_H */
