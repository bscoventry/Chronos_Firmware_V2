# Chronos nRF54L15 — pin reference

Firmware pin usage for **SPI (single DAC)** and **analog-switch GPIO** driven from `timer.c`.

Custom board: **QFN52 QGAA** (P1.00–P1.16 on port 1). DK LED library disabled (`CONFIG_DK_LIBRARY=n`).

## All GPIO pins (summary)

| Port.pin | Direction | Function |
|----------|-----------|----------|
| **P0.23** | Out | NUS UART TX (`app.overlay`) |
| **P0.25** | In | NUS UART RX (`app.overlay`) |
| **P0.05** | Out | AD5933 electrode-route switch (`zmeas_gpio.c`; off at boot) |
| **P1.02** | Out | Analog switch — interphase (`timer.c`, `main.c`) |
| **P1.03** | Out | Analog switch — second pulse (`timer.c`, `main.c`) |
| **P1.08** | I2C | TWIM SCL → AD5933 (`i2c22`, Phase A) |
| **P1.09** | Out | SPI DAC chip select (GPIO, active low; `spi.h` / `spi.c`) |
| **P1.10** | Out | SPI MOSI / SPIM21 (`spi.h`, `spi.c`) |
| **P1.11** | Out | SPI SCK / SPIM21 (`spi.h`, `spi.c`) |
| **P1.12** | I2C | TWIM SDA → AD5933 (`i2c22`, Phase A) |
| **P1.15** | In | Reed switch input (`ble_session.c`, pull-down) |
| **P1.16** | Out | Analog switch — first pulse (`timer.c`, `main.c`; QFN52 phys pin 6) |

## SPI (nrfx SPIM21, `spi.h` / `spi.c`)

| Signal | Port.pin | Notes |
|--------|----------|--------|
| SCK | **P1.11** | Serial clock |
| MOSI | **P1.10** | Data to DAC |
| CS | **P1.09** | Chip select, GPIO (active low) |
| MISO | — | Not used; disconnected in devicetree pinctrl |

Single DAC: both biphasic phases use `spi_write_dac1()` / **P1.09** CS. Phase-2 codes live in `dac2_buf_tx` (opposite amplitude).

## GPIO analog-switch control (`timer.c`)

| Port.pin | Role |
|----------|------|
| **P1.16** | First pulse |
| **P1.02** | Interphase (25 µs) |
| **P1.03** | Second pulse |

At startup all three are low (`init_pins()`).

Timer phase behavior:
- First pulse: `P1.16=1`, `P1.02=0`, `P1.03=0`
- Interphase: `P1.16=0`, `P1.02=1`, `P1.03=0`
- Second pulse: `P1.16=0`, `P1.02=0`, `P1.03=1`
- After second pulse: `P1.16=0`, `P1.02=0`, `P1.03=0`

## Impedance monitor (`zmeas_gpio.c`, `ad5933.c`, Phase A)

| Port.pin | Role |
|----------|------|
| **P0.05** | Routes electrodes to AD5933 path when high |
| **P1.08** | I2C SCL (TWIM22) |
| **P1.12** | I2C SDA (TWIM22) |

Use **i2c22**, not i2c21 — i2c21 shares the peripheral block with nrfx **SPIM21**.

## BLE reed switch (`ble_session.c`)

| Signal | Port.pin | Notes |
|--------|----------|--------|
| Reed switch input | **P1.15** | Pull-down; NO reed to VCC on swipe |

## See also

- `src/spi.h` — `SCK_PIN`, `MOSI_PIN`, `DAC_CS_PIN`
- `src/timer.h` — `SWITCH_PIN_FIRST`, `SWITCH_PIN_INTER`, `SWITCH_PIN_SECOND`
- `app.overlay` — SPIM21 pinctrl (SCK P1.11, MOSI P1.10)
- `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` — `gpio1` `ngpios = <17>` for P1.16
