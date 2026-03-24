#include <nrfx_spim.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <hal/nrf_gpio.h>
#include <string.h>
#include "spi.h"
#include "config.h"

/* SPIM21 on P1.05/1.06; nrfx configures GPIO+PSEL (no pinctrl). */
static nrfx_spim_t spim_inst = NRFX_SPIM_INSTANCE(NRF_SPIM21);
int spi_ok;
static void spim_handler(nrfx_spim_event_t const * p_event, void * p_context);
uint8_t dac1_buf_tx[DAC_TX_LEN] = {0x52, 0x53};
uint8_t dac2_buf_tx[DAC_TX_LEN] = {0x54, 0x55};
uint8_t dac1_buf_rx[DAC_RX_LEN];
uint8_t dac2_buf_rx[DAC_RX_LEN];
void update_dac1_amplitude(uint16_t amplitude) {
    dac1_buf_tx[0] = (amplitude >> 8) & 0xFF;  // MSB
    dac1_buf_tx[1] = amplitude & 0xFF;         // LSB

    printf("DAC1 amplitude updated to %u (0x%02X 0x%02X)\n",
           amplitude, dac1_buf_tx[0], dac1_buf_tx[1]);
}

void update_dac2_amplitude(uint16_t amplitude) {
    uint16_t opposite_amplitude;
    if (amplitude == 0x0000) {
        opposite_amplitude = 0xFFFF;  // Most negative → Most positive
    } else {
        opposite_amplitude = (uint16_t)(0x10000UL - amplitude);
    }

    dac2_buf_tx[0] = (opposite_amplitude >> 8) & 0xFF;  // MSB
    dac2_buf_tx[1] = opposite_amplitude & 0xFF;         // LSB

    printf("DAC2 amplitude updated to opposite of %u: %u (0x%02X 0x%02X)\n",
           amplitude, opposite_amplitude, dac2_buf_tx[0], dac2_buf_tx[1]);
}

void cs_select(uint32_t pin_number) {
    nrf_gpio_pin_clear(pin_number);  // Drive CS low (active)
}

void cs_deselect(uint32_t pin_number) {
    nrf_gpio_pin_set(pin_number);     // Drive CS high (inactive)
}

void spi_write_dac1(uint8_t *tx_data, uint8_t *rx_data) {
    NRF_SPIM_Type *sp = spim_inst.p_reg;
    /* Clear SPIM events before this transfer. */
    sp->EVENTS_STARTED = 0;
    sp->EVENTS_END = 0;
    sp->EVENTS_STOPPED = 0;

    cs_select(DAC1_CS_PIN);
    memset(rx_data, 0, DAC_RX_LEN);
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(tx_data, DAC_TX_LEN, rx_data, DAC_RX_LEN);
    nrfx_err_t err = nrfx_spim_xfer(&spim_inst, &xfer_desc, 0);

    /* Wait for transfer completion before releasing CS.
     * nrfx_spim_xfer() is not completion-synchronous unless an END IRQ handler runs,
     * so polling EVENTS_END/STOPPED keeps CS aligned to the actual SCK/MOSI burst. */
    const uint32_t timeout_iters = 20000u;
    uint32_t timeout = timeout_iters;
    while (timeout-- > 0) {
        if (sp->EVENTS_END || sp->EVENTS_STOPPED) {
            break;
        }
    }
    int timed_out = !(sp->EVENTS_END || sp->EVENTS_STOPPED);

    for (volatile uint32_t i = 0; i < SPI_CS_HOLD_DELAY_LOOPS; i++) {
        (void)i;
    }
    cs_deselect(DAC1_CS_PIN);

#if (SPI_VERBOSE == 1)
    printf("spi_write_dac1: tx[0]=0x%02X tx[1]=0x%02X err=%d (0x%08X) "
           "events: STARTED=%lu END=%lu STOPPED=%lu ENABLE=0x%lX timeout=%lu\n",
           tx_data[0], tx_data[1], (int)err, (unsigned int)err,
           (unsigned long)sp->EVENTS_STARTED,
           (unsigned long)sp->EVENTS_END,
           (unsigned long)sp->EVENTS_STOPPED,
           (unsigned long)sp->ENABLE,
           (unsigned long)(timeout_iters - timeout));
#endif
    if (err != 0 && SPI_VERBOSE == 0) {
        /* Keep errors visible even with SPI_VERBOSE off. */
        printf("spi_write_dac1: nrfx_spim_xfer error=%d\n", (int)err);
    }
    if (timed_out && SPI_VERBOSE == 0) {
        printf("spi_write_dac1: SPIM completion timeout (END/STOPPED not set)\n");
    }
}

void spi_write_dac2(uint8_t *tx_data, uint8_t *rx_data) {
    cs_select(DAC2_CS_PIN);
    memset(rx_data, 0, DAC_RX_LEN);
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(tx_data, DAC_TX_LEN, rx_data, DAC_RX_LEN);
    nrfx_err_t err = nrfx_spim_xfer(&spim_inst, &xfer_desc, 0);

    /* Match CS to the actual transfer duration. */
    const uint32_t timeout_iters = 20000u;
    uint32_t timeout = timeout_iters;
    NRF_SPIM_Type *sp = spim_inst.p_reg;
    while (timeout-- > 0) {
        if (sp->EVENTS_END || sp->EVENTS_STOPPED) {
            break;
        }
    }
    int timed_out = !(sp->EVENTS_END || sp->EVENTS_STOPPED);

    for (volatile uint32_t i = 0; i < SPI_CS_HOLD_DELAY_LOOPS; i++) {
        (void)i;
    }
    cs_deselect(DAC2_CS_PIN);

#if (SPI_VERBOSE == 1)
    printf("spi_write_dac2: err=%d (0x%08X) events: STARTED=%lu END=%lu STOPPED=%lu\n",
           (int)err, (unsigned int)err,
           (unsigned long)sp->EVENTS_STARTED,
           (unsigned long)sp->EVENTS_END,
           (unsigned long)sp->EVENTS_STOPPED);
#endif
    if (err != 0 && SPI_VERBOSE == 0) {
        printf("spi_write_dac2: nrfx_spim_xfer error=%d\n", (int)err);
    }
    if (timed_out && SPI_VERBOSE == 0) {
        printf("spi_write_dac2: SPIM completion timeout (END/STOPPED not set)\n");
    }
}
void spi_init(void)
{
    nrfx_spim_config_t spim_config = NRFX_SPIM_DEFAULT_CONFIG(SCK_PIN,
                                                              MOSI_PIN,
                                                              NRF_SPIM_PIN_NOT_CONNECTED,
                                                              NRF_SPIM_PIN_NOT_CONNECTED);
    spim_config.frequency = 2000000;
    nrfx_err_t status = nrfx_spim_init(&spim_inst, &spim_config, NULL, NULL);
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

    /*
     * nrfx leaves SPIM disabled after init and enables only during each xfer,
     * then disables again when NRF_SPIM_CHECK_DISABLE_ON_XFER_END is set.
     * Enable now so SPIM stays on; the driver will then leave it enabled after
     * the first transfer (disable_on_xfer_end = false when already enabled).
     */
    if (status == 0) {
        nrf_spim_enable(spim_inst.p_reg);
    }

#if NRF_GPIO_HAS_SEL
    /* nRF54L: CTRLSEL_GPIO (0) = "GPIO or peripherals with PSEL". Mux SCK/MOSI to
     * that domain after SPIM init so SPIM20 (which has PSEL set) drives the pins.
     */
    nrf_gpio_pin_control_select(SCK_PIN, NRF_GPIO_PIN_SEL_GPIO);
    nrf_gpio_pin_control_select(MOSI_PIN, NRF_GPIO_PIN_SEL_GPIO);
#endif

    /* Diagnostic: dump SPIM21 and P1 pin config registers */
    {
        NRF_SPIM_Type *sp = spim_inst.p_reg;
        printf("SPIM21 reg dump: ENABLE=0x%lX PSEL.SCK=%lu PSEL.MOSI=%lu\n",
               (unsigned long)sp->ENABLE,
               (unsigned long)sp->PSEL.SCK,
               (unsigned long)sp->PSEL.MOSI);
        uint32_t pins[] = { SCK_PIN, MOSI_PIN, DAC1_CS_PIN, DAC2_CS_PIN };
        const char *names[] = { "P1.11 SCK", "P1.06 MOSI", "P1.09 CS1", "P1.14 CS2" };
        for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
            uint32_t p = pins[i];
            NRF_GPIO_Type *port = nrf_gpio_pin_port_decode(&p);
            printf("  %s PIN_CNF=0x%08lX\n", names[i], (unsigned long)port->PIN_CNF[p]);
        }
    }
}

__attribute__((unused))
static void spim_handler(nrfx_spim_event_t const * p_event, void * p_context){
    if ((p_event->type == NRFX_SPIM_EVENT_DONE) && (SPI_VERBOSE == 1)){
        uint8_t first_byte = p_event->xfer_desc.p_rx_buffer ? *p_event->xfer_desc.p_rx_buffer : 0;
        printf("Message received: %02X\n", first_byte);
    }
}
