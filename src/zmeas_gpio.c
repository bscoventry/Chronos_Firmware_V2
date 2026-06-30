/*
 * Impedance-monitor analog switch control (P0.05).
 */
#include <hal/nrf_gpio.h>

#include "zmeas_gpio.h"

#if defined(CONFIG_CHRONOS_ZMEAS)

void zmeas_gpio_init(void)
{
	nrf_gpio_cfg_output(ZMEAS_SWITCH_PIN);
	zmeas_gpio_switch_set(false);
}

void zmeas_gpio_switch_set(bool connect)
{
#if defined(CONFIG_CHRONOS_ZMEAS_SWITCH_ACTIVE_HIGH)
	const bool level = connect;
#else
	const bool level = !connect;
#endif

	if (level) {
		nrf_gpio_pin_set(ZMEAS_SWITCH_PIN);
	} else {
		nrf_gpio_pin_clear(ZMEAS_SWITCH_PIN);
	}
}

#else /* !CONFIG_CHRONOS_ZMEAS */

void zmeas_gpio_init(void)
{
}

void zmeas_gpio_switch_set(bool connect)
{
	ARG_UNUSED(connect);
}

#endif /* CONFIG_CHRONOS_ZMEAS */
