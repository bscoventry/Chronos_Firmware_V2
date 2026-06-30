#include <zephyr/types.h>
#include <string.h>
#include <stdio.h>

#include "data.h"
#include "config.h"
#include "spi.h"
#include "timer.h"
#if defined(CONFIG_BT) && !defined(CONFIG_SPI) && defined(CONFIG_CHRONOS_STIM_NVM)
#include "stim_nvm.h"
#endif

stim_setting settings;
stim_setting pending_settings;
uint8_t ble_received_data[BLE_DATA_BUFFER_SIZE];
uint16_t ble_data_length;

void data_init_defaults(void)
{
	settings.DAC_amplitude = CONFIG_STIM_AMPLITUDE;
	settings.pulse_width = (uint16_t)CONFIG_PULSE_WIDTH_US;
	settings.frequency = (uint16_t)CONFIG_STIM_FREQUENCY_HZ;
	pending_settings = settings;
	update_dac1_amplitude(settings.DAC_amplitude);
	update_dac2_amplitude(settings.DAC_amplitude);
}

#if defined(CONFIG_BT)

static void stimulation_engine_start(const stim_setting *s)
{
	if (s->frequency == 0u || s->pulse_width == 0u) {
		printf("Stim start ignored: invalid frequency or pulse width\n");
		return;
	}

#if defined(CONFIG_CHRONOS_STIM_NVM)
	(void)stim_nvm_save(true, s);
#endif
	update_stim_frequency(s->frequency);
	update_pulse_width(s->pulse_width);
	update_dac1_amplitude(s->DAC_amplitude);
	update_dac2_amplitude(s->DAC_amplitude);
	timer_stimulation_enable();
}

void process_received_data(stim_setting *active_settings, uint8_t *data, uint16_t len)
{
	if (len != STIM_PACKET_LEN) {
		printf("BLE packet length %u (expected %u)\n", len, (unsigned int)STIM_PACKET_LEN);
		return;
	}

	const uint8_t ctrl = data[0];
	stim_setting payload;

	memcpy(&payload, data + 1, sizeof(payload));

	const bool running = timer_stimulation_is_enabled();

	if (ctrl == STIM_CTRL_START) {
		memcpy(active_settings, &payload, sizeof(stim_setting));
		memcpy(&pending_settings, &payload, sizeof(stim_setting));
		if (running) {
#if defined(CONFIG_CHRONOS_STIM_NVM)
			(void)stim_nvm_save(true, &payload);
#endif
			update_stim_frequency(payload.frequency);
			update_pulse_width(payload.pulse_width);
			update_dac1_amplitude(payload.DAC_amplitude);
			update_dac2_amplitude(payload.DAC_amplitude);
			return;
		}
		stimulation_engine_start(active_settings);
		return;
	}

	if (ctrl == STIM_CTRL_STOP) {
		if (running) {
#if defined(CONFIG_CHRONOS_STIM_NVM)
			(void)stim_nvm_save(false, active_settings);
#endif
			stim_timer_request_stop_after_burst();
		} else {
#if defined(CONFIG_CHRONOS_STIM_NVM)
			(void)stim_nvm_save(false, &payload);
#endif
			memcpy(&pending_settings, &payload, sizeof(stim_setting));
			update_dac1_amplitude(pending_settings.DAC_amplitude);
			update_dac2_amplitude(pending_settings.DAC_amplitude);
		}
		return;
	}

	printf("BLE unknown control byte 0x%02x\n", ctrl);
}

#else /* !CONFIG_BT */

void process_received_data(stim_setting *active_settings, uint8_t *data, uint16_t len)
{
	ARG_UNUSED(active_settings);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
}

#endif /* CONFIG_BT */
