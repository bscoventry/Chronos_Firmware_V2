#ifndef RTC_STIM_H
#define RTC_STIM_H

#include <stdint.h>
#include <stdbool.h>

/** Start LFCLK (RC). Idempotent; optional before rtc_stim_init. */
void rtc_stim_start_lfclk(void);

/** Stop periodic stimulation scheduling (TIMER20). TIMER00 burst may still complete. */
void rtc_stim_stop(void);

/** True if TIMER20 period scheduling is enabled. */
bool rtc_stim_is_running(void);

/** Update period while TIMER20 is active (no-op if not initialized). */
void rtc_stim_set_frequency(uint16_t frequency_hz);

/**
 * Enable low-power periodic stimulation: TIMER20 compare every (1/frequency_hz) s.
 * Each tick ensures HFCLK, runs timer_do_event0(), then TIMER00 one-shot biphasic.
 */
void rtc_stim_init(uint16_t frequency_hz);

#endif /* RTC_STIM_H */
