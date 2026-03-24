/*
Author: Brandon S Coventry      Wisconsin Institute for Translational Neuroengineering
Date: 2026/02/12 Initialization
Purpose: Chronos firmware ported for the nRF54L15
Revision History: See Github history
Notes:
 - Includes the ability to turn bluetooth transmission on and off in config file. Set CONFIG_BT in config file to 0 to turn off BLE engine
 - Primary stimulation driver is still and interupt based routine. 
*/

/*Basic includes. Minimize to limit current draw from unused engines.*/
//Zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/irq.h> //Interrupt service routine handlers
#include <zephyr/device.h>  //Must import devicetree files
#include <zephyr/devicetree.h>
#include <soc.h>    //Standard System on chip imports
#include <zephyr/logging/log.h>   //Disable on compile
#include <inttypes.h>

#if defined(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#endif

//nRFX imports. Note we are primarily runnig
#include <nrfx_spim.h>
#include <nrfx_timer.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_clock.h>

#if defined(CONFIG_SPI)
#include <zephyr/drivers/spi.h>
#endif

//Chronos engine imports 
#include "BLE.h"  //BLE engine
#include "ble_session.h"
#include "spi.h" //SPI to howland current source
#include "timer.h"  //Handles interrupt timing of stimulation
#include "rtc_stim.h"   //handles IRQ routines for stimulation
#include "data.h"
#include "config.h"   //Set compilation settings. Look here for CONFIG_BT

#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_INF);

//Add UART Driver only if BLE is active. Note, UART is *virtual*, but still requires pin definitions. 
#if defined(CONFIG_BT)
#include <uart_async_adapter.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#endif

//Set up peripherial initialization
static void init_clock();   //Turn on stimulation timing engine
#if defined(CONFIG_BT)
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};
#endif /* CONFIG_BT */

static void init_pins(void) {
    /* SPI CS and switches are GPIO. When CONFIG_SPI=y the Zephyr driver owns DAC1_CS (P1.08 in Zephyr devicetree). */
#if !defined(CONFIG_SPI)
    nrf_gpio_cfg_output(DAC1_CS_PIN);
    nrf_gpio_pin_set(DAC1_CS_PIN);   /* high (inactive) */
#endif
    nrf_gpio_cfg_output(DAC2_CS_PIN);
    nrf_gpio_pin_set(DAC2_CS_PIN);   /* high (inactive) */
    
    // Switch GPIOs (DAC1/2 -> switch): all 0 at init
    //HCSS1
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 7));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 7)); //set low (inactive)
    //HCSS2
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 9));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 9)); //set low (inactive)
    //HCSS3
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 8));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 8));
    //HCSS4
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 0));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 0));
}

int main(void)
{
    //Begin with system initialization
#if defined(CONFIG_BT)
    init_clock();
#endif
    init_pins();
#if defined(CONFIG_SPI)
    /* Zephyr SPI test build: no nrfx SPI/timer activity here. */
    k_msleep(10);
#else
    /* nrfx build: initialize SPI once; transactions are driven from timer ISR. */
    spi_init();
#endif
#if defined(CONFIG_BT)
    timer_init();
#else
    timer_burst_init();
#endif
    update_pulse_width(CONFIG_PULSE_WIDTH_US);
    update_dac1_amplitude(CONFIG_STIM_AMPLITUDE);
    update_dac2_amplitude(CONFIG_STIM_AMPLITUDE);

    data_init_defaults();

#if defined(CONFIG_BT)
	/* Stimulation timer stays off until START control byte (see data.c). */
	measurement_timer_init();
	int err = 0;
	uint32_t experiment_counter = 0;

	configure_gpio();
	err = uart_init();
	if (err) {
		error();
	}

	if (IS_ENABLED(CONFIG_BT_NUS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			LOG_ERR("Failed to register authorization callbacks. (err: %d)", err);
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			LOG_ERR("Failed to register authorization info callbacks. (err: %d)", err);
			return 0;
		}
	}

	ble_session_init();
	LOG_INF("Toggle reed P1.04 to enable BLE; stimulation off until START byte");

	/* RUN_STATUS_LED is driven by ble_session (on while BLE session active). */
	for (;;) {
		if (MEASURE_TIMER == 1) {
			k_msleep(10000);
			experiment_counter += 10;
			error_data my_error_data;
			get_error_data(&my_error_data);
			printf("Counter: %i Elapsed: %is\nEvent0 running error: %" PRIu32
			       " avg error: %" PRIu32 " max error: %" PRIu32 "\n"
			       "Events1-3 running error: %" PRIu32 " avg error: %" PRIu32
			       " max error: %" PRIu32 ", %" PRIu32 ", %" PRIu32 "\n",
			       my_error_data.mycounter,
			       experiment_counter,
			       my_error_data.event0_error,
			       (my_error_data.event0_error / my_error_data.mycounter),
			       my_error_data.event0_max,
			       my_error_data.myerror,
			       (my_error_data.myerror / my_error_data.mycounter),
			       my_error_data.event1_max,
			       my_error_data.event2_max,
			       my_error_data.event3_max);
		} else {
			k_sleep(K_FOREVER);
		}
	}
    #else
        /* RTC low-power: no BLE; RTC wakes every stim period, timer runs one biphasic burst */
        rtc_stim_start_lfclk();
        rtc_stim_init(CONFIG_STIM_FREQUENCY_HZ);
        LOG_INF("RTC-driven stimulation at %u Hz (no BLE)", CONFIG_STIM_FREQUENCY_HZ);
        for (;;) {
            k_sleep(K_FOREVER);
        }
    #endif /* CONFIG_BT */
    }

    K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
            NULL, PRIORITY, 0, 0);

    static void init_clock() {
#if !defined(CONFIG_SOC_NRF54L15)
        nrf_clock_hfclk_src_set(NRF_CLOCK_S, NRF_CLOCK_HFCLK_HIGH_ACCURACY);
#endif
        nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_HFCLKSTART);
        while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
            /* spin */
        }
        nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED);
    }
