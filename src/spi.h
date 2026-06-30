#ifndef SPI_H
#define SPI_H
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <hal/nrf_gpio.h>

/* SPIM21 pins — single DAC, CS on P1.09 */
#define DAC_CS_PIN   NRF_GPIO_PIN_MAP(1, 9)   /* P1.09 */
#define DAC1_CS_PIN  DAC_CS_PIN
#define DAC_TX_LEN   2
#define DAC_RX_LEN   2

/* SPIM21 */
#define SPIM_INST_IDX 21
#define SCK_PIN   NRF_GPIO_PIN_MAP(1, 11)  /* P1.11 */
#define MOSI_PIN  NRF_GPIO_PIN_MAP(1, 10)  /* P1.10 */

void cs_select(uint32_t pin_number);
void cs_deselect(uint32_t pin_number);
void spi_write_dac1(uint8_t *tx_data, uint8_t *rx_data);
void spi_init();
void update_dac1_amplitude(uint16_t amplitude);
void update_dac2_amplitude(uint16_t amplitude);

/** Incremented on nrfx_spim_xfer error or wait timeout (stale DAC latch if non-zero). */
extern uint32_t spi_xfer_fail_count;

extern uint8_t dac1_buf_rx[DAC_RX_LEN];
extern uint8_t dac1_buf_tx[DAC_TX_LEN];
extern uint8_t dac2_buf_rx[DAC_RX_LEN];
extern uint8_t dac2_buf_tx[DAC_TX_LEN];
#endif
