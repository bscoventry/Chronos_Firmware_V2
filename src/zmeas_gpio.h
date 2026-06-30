#ifndef ZMEAS_GPIO_H
#define ZMEAS_GPIO_H

#include <stdbool.h>

/** P0.05 — routes electrodes to AD5933 front-end when asserted (active level from Kconfig). */
#define ZMEAS_SWITCH_PIN  NRF_GPIO_PIN_MAP(0, 5)

/** Configure P0.05 as output; switch off (stim path) at boot. */
void zmeas_gpio_init(void);

/** True = connect electrodes to impedance monitor path. */
void zmeas_gpio_switch_set(bool connect);

#endif /* ZMEAS_GPIO_H */
