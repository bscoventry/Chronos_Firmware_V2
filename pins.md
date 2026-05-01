# Chronos nRF54L15 — pin reference

Firmware pin usage for **SPI (DAC)** and **analog-switch GPIO** driven from `timer.c`.

## All GPIO pins (summary)

Sorted by port and pin index. One row per physical pin; details below by subsystem.

| Port.pin | Direction | Function |
|----------|-----------|----------|
| **P0.23** | Out | NUS UART TX (`app.overlay`) |
| **P0.25** | In | NUS UART RX (`app.overlay`) |
| **P1.05** | Out | Analog switch control (`timer.c`, `main.c`) |
| **P1.06** | Out | SPI MOSI / SPIM21 (`spi.h`, `spi.c`) |
| **P1.07** | Out | Analog switch control (`timer.c`, `main.c`) |
| **P1.09** | Out | DAC1 chip select (GPIO, active low; `spi.h` / `spi.c`) |
| **P1.10** | In | Reed switch input (`ble_session.c`) |
| **P1.11** | Out | SPI SCK / SPIM21 (`spi.h`, `spi.c`) |
| **P1.13** | Out | Analog switch control (`timer.c`, `main.c`) |
| **P1.14** | Out | DAC2 chip select (GPIO, active low; `spi.h` / `spi.c`) |

**DK LEDs / buttons:** `DK_LED1`, `DK_LED2` (and optional passkey buttons when `CONFIG_BT_NUS_SECURITY_ENABLED`) — pins come from the board devicetree (`dk_buttons_and_leds`), not fixed in application source.

**NUS UART on other boards:** Top-level `app.overlay` uses **P0.23 / P0.25** for `&uart00`. nRF54L15 DK board overlays sometimes enable a different UART instance for NUS (e.g. UART30); confirm in `boards/*cpuapp*.overlay` if you build for that target.

## SPI (nrfx SPIM21, `spi.h` / `spi.c`)

| Signal   | Port.pin | Notes                          |
|----------|----------|--------------------------------|
| SCK      | **P1.11** | Serial clock                   |
| MOSI     | **P1.06** | Data to DAC                    |
| DAC1 CS  | **P1.09** | Chip select, GPIO (active low) |
| DAC2 CS  | **P1.14** | Chip select, GPIO (active low) |
| MISO     | —         | Not used; disconnected in devicetree pinctrl |

## GPIO analog-switch control (`timer.c`)

These lines are toggled in the timer ISR around SPI phases (`timer_do_event0` / `timer_handler`).

| Port.pin | Role in comments / sequence |
|----------|-----------------------------|
| **P1.07** | Merged switch line (replaces former P0.02 + P0.03) |
| **P1.05** | Switch control (replaces former P0.04) |
| **P1.13** | Switch control (replaces former P0.05) |

At startup all three are low (`init_pins()`): `P1.13=0`, `P1.07=0`, `P1.05=0`.

Timer phase behavior in `timer.c`:
- First pulse: `P1.13=1`, `P1.07=0`, `P1.05=0`
- Interphase: `P1.13=0`, `P1.07=1`, `P1.05=0`
- Second pulse: `P1.13=0`, `P1.07=0`, `P1.05=1`
- After second pulse: `P1.13=0`, `P1.07=0`, `P1.05=0`

## BLE reed switch (`ble_session.c`)

| Signal | Port.pin | Notes |
|--------|----------|-------|
| Reed switch input | **P1.10** | GPIO input with pull-up; magnetic toggle enables/disables BLE session |

## See also

- `src/spi.h` — macro names `SCK_PIN`, `MOSI_PIN`, `DAC1_CS_PIN`, `DAC2_CS_PIN`
- `app.overlay` — SPIM21 pinctrl (SCK/MOSI; MISO disconnected)
- `src/ble_session.c` — magnetic reed on **P1.10** (separate from `timer.c` switch GPIOs)
