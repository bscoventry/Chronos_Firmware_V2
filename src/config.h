#ifndef CONFIG_H
#define CONFIG_H

#define MEASURE_TIMER 0 // 1: testing timer accuracy with built-in timer
                        // 0: disable measurement timer
#define SPI_VERBOSE 0   // 1: enable verbose SPI logging

/* CS hold delay: loops after nrfx_spim_xfer before cs_deselect (~2-3 us). Tune via scope:
 * Increase if CS rises before SCLK finishes; decrease if pulse is longer than needed.
 * At 64 MHz: ~32-64 = 2 us, ~64-96 = 3 us. */
#define SPI_CS_HOLD_DELAY_LOOPS  64

/* Biphasic square-wave stimulation (compile-time; overrides BLE) */
#define CONFIG_STIM_AMPLITUDE        0xFFAA   /* DAC amplitude (16-bit). Phase 2 = opposite. */
#define CONFIG_PULSE_WIDTH_US        200u      /* Pulse duration per phase (us). Typically 200 us. */
#define CONFIG_INTER_PHASE_GAP_US    10u       /* Gap between phase 1 and phase 2 (us). Typically 10 us.
                                                  Note: Actual gap is SWITCH_PERIOD in timer.h; timer not modified. */
#define CONFIG_STIM_FREQUENCY_HZ     130u      /* Biphasic pulse rate (Hz). Typically 130 Hz. */

#endif // CONFIG_H
