#include <nrfx_spim.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_clock.h>
#include <string.h>
#include <stdio.h>
#include "spi.h"
#include "config.h"

/* SPIM21 on P1.10 (MOSI), P1.11 (SCK); nrfx configures GPIO+PSEL (no pinctrl). */
static nrfx_spim_t spim_inst = NRFX_SPIM_INSTANCE(NRF_SPIM21);
int spi_ok;

/* DAC8832: SPI mode 0 (CPOL=0, CPHA=0) — MOSI updates on SCK falling, DAC latches on rising. */
/* Above TIMER20/00 (prio 2) so END IRQ runs while timer ISR waits on xfer. */
#define SPIM_IRQ_PRIO  1

#define NRFX_ERR_BUSY  13

static volatile bool spim_xfer_done;
uint32_t spi_xfer_fail_count;

static void spim_log_xfer_error(const char *msg, int err)
{
	if (!k_is_in_isr()) {
		printf("%s err=%d (fail count=%lu)\n", msg, err,
		       (unsigned long)spi_xfer_fail_count);
	}
}

/* Wait until nrfx SPIM is idle (required after abort before next xfer). */
static void spim_ensure_idle(void)
{
	NRF_SPIM_Type *sp = spim_inst.p_reg;

	if (!spim_inst.cb.transfer_in_progress) {
		return;
	}

	nrfx_spim_abort(&spim_inst);
	for (uint32_t spin = 100000u; spin > 0u && spim_inst.cb.transfer_in_progress; spin--) {
		if (sp->EVENTS_END) {
			nrfx_spim_irq_handler(&spim_inst);
		}
	}
	spim_xfer_done = false;
}

static void spim_prepare_next_xfer(void)
{
	spim_ensure_idle();
	spim_xfer_done = false;
}

/* nRF54L: END must be processed in nrfx_spim_irq_handler (errata 55). */
static void spim_wait_xfer_done(void)
{
	NRF_SPIM_Type *sp = spim_inst.p_reg;

	/* SPIM IRQ (prio 1) preempts timer ISR (prio 2); spin for handler flag first. */
	for (uint32_t spin = 200000u; spin > 0u && !spim_xfer_done; spin--) {
		if (sp->EVENTS_END) {
			nrfx_spim_irq_handler(&spim_inst);
		}
	}
	if (!spim_xfer_done && sp->EVENTS_END) {
		nrfx_spim_irq_handler(&spim_inst);
	}
	if (!spim_xfer_done) {
		nrfx_spim_abort(&spim_inst);
		spim_ensure_idle();
		spi_xfer_fail_count++;
		spim_log_xfer_error("SPI xfer timeout/abort", -1);
	}
}

static void dac_ensure_hfclk(void)
{
	nrf_clock_task_trigger(NRF_CLOCK_S, NRF_CLOCK_TASK_HFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
		/* spin */
	}
	nrf_clock_event_clear(NRF_CLOCK_S, NRF_CLOCK_EVENT_HFCLKSTARTED);
}

static void spim_handler(nrfx_spim_event_t const *p_event, void *p_context)
{
	(void)p_context;

	if (p_event->type == NRFX_SPIM_EVENT_DONE) {
		spim_xfer_done = true;
	}
}

uint8_t dac1_buf_tx[DAC_TX_LEN] = {0x52, 0x53};
uint8_t dac2_buf_tx[DAC_TX_LEN] = {0x54, 0x55};
uint8_t dac1_buf_rx[DAC_RX_LEN];
uint8_t dac2_buf_rx[DAC_RX_LEN];

void update_dac1_amplitude(uint16_t amplitude)
{
	unsigned int key = irq_lock();

	dac1_buf_tx[0] = (amplitude >> 8) & 0xFF;
	dac1_buf_tx[1] = amplitude & 0xFF;
	irq_unlock(key);

	printf("DAC1 code set to 0x%04X\n", amplitude);
}

void update_dac2_amplitude(uint16_t amplitude)
{
	uint16_t opposite_amplitude;

	if (amplitude == 0x0000) {
		opposite_amplitude = 0x8000;
	} else {
		opposite_amplitude = (uint16_t)(0x10000UL - amplitude);
	}

	unsigned int key = irq_lock();

	dac2_buf_tx[0] = (opposite_amplitude >> 8) & 0xFF;
	dac2_buf_tx[1] = opposite_amplitude & 0xFF;
	irq_unlock(key);

	printf("DAC2 code set to 0x%04X (complement of 0x%04X)\n",
	       opposite_amplitude, amplitude);
}

void cs_select(uint32_t pin_number)
{
	nrf_gpio_pin_clear(pin_number);
}

void cs_deselect(uint32_t pin_number)
{
	nrf_gpio_pin_set(pin_number);
}

void spi_write_dac1(uint8_t *tx_data, uint8_t *rx_data)
{
	ARG_UNUSED(rx_data);

	dac_ensure_hfclk();
	spim_prepare_next_xfer();

	cs_select(DAC1_CS_PIN);

	nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TX(tx_data, DAC_TX_LEN);
	int err = nrfx_spim_xfer(&spim_inst, &xfer_desc, 0);

	if (err == NRFX_ERR_BUSY) {
		spim_ensure_idle();
		err = nrfx_spim_xfer(&spim_inst, &xfer_desc, 0);
	}

	if (err != 0) {
		cs_deselect(DAC1_CS_PIN);
		spi_xfer_fail_count++;
		spim_log_xfer_error("SPI xfer start", err);
		return;
	}

	spim_wait_xfer_done();

	for (volatile uint32_t i = 0; i < SPI_CS_HOLD_DELAY_LOOPS; i++) {
		(void)i;
	}
	cs_deselect(DAC1_CS_PIN);

#if (SPI_VERBOSE == 1)
	if (!k_is_in_isr()) {
		NRF_SPIM_Type *sp = spim_inst.p_reg;

		printf("spi_write_dac1: tx[0]=0x%02X tx[1]=0x%02X err=%d done=%d END=%lu\n",
		       tx_data[0], tx_data[1], err, (int)spim_xfer_done,
		       (unsigned long)sp->EVENTS_END);
	}
#endif
}

void spi_init(void)
{
	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_SPIM21), SPIM_IRQ_PRIO,
		    nrfx_spim_irq_handler, &spim_inst, 0);

	nrfx_spim_config_t spim_config = NRFX_SPIM_DEFAULT_CONFIG(SCK_PIN,
								  MOSI_PIN,
								  NRF_SPIM_PIN_NOT_CONNECTED,
								  NRF_SPIM_PIN_NOT_CONNECTED);
	spim_config.frequency = CONFIG_SPI_FREQUENCY_HZ;
	spim_config.irq_priority = SPIM_IRQ_PRIO;
	/* NRFX_SPIM_DEFAULT_CONFIG: NRF_SPIM_MODE_0, MSB first */

	nrfx_err_t status = nrfx_spim_init(&spim_inst, &spim_config, spim_handler, NULL);
	spi_ok = (status == 0);
	printf("nrfx_spim_init status=%d (0x%08X)\n",
	       status, (unsigned int)status);
	printf("spim_inst.p_reg=%p, NRF_SPIM21=%p\n",
	       (void *)spim_inst.p_reg, (void *)NRF_SPIM21);
	if (status == 0) {
		printf("SPI initialized successfully on SPIM%d\n", SPIM_INST_IDX);
		printf("  SCK: P%d.%02d  MOSI: P%d.%02d  MISO: NC\n",
		       (SCK_PIN >> 5), (SCK_PIN & 0x1F), (MOSI_PIN >> 5), (MOSI_PIN & 0x1F));
	} else {
		printf("SPI initialization failed with error: %d\n", status);
	}

#if NRF_GPIO_HAS_SEL
	nrf_gpio_pin_control_select(SCK_PIN, NRF_GPIO_PIN_SEL_GPIO);
	nrf_gpio_pin_control_select(MOSI_PIN, NRF_GPIO_PIN_SEL_GPIO);
#endif

	{
		NRF_SPIM_Type *sp = spim_inst.p_reg;

		printf("SPIM21 reg dump: ENABLE=0x%lX PSEL.SCK=%lu PSEL.MOSI=%lu\n",
		       (unsigned long)sp->ENABLE,
		       (unsigned long)sp->PSEL.SCK,
		       (unsigned long)sp->PSEL.MOSI);
		uint32_t pins[] = { SCK_PIN, MOSI_PIN, DAC_CS_PIN };
		const char *names[] = { "P1.11 SCK", "P1.10 MOSI", "P1.09 CS" };

		for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
			uint32_t p = pins[i];
			NRF_GPIO_Type *port = nrf_gpio_pin_port_decode(&p);

			printf("  %s PIN_CNF=0x%08lX\n", names[i], (unsigned long)port->PIN_CNF[p]);
		}
	}
}
