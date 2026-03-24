# SPI test: nrfx vs Zephyr driver

## Default (nrfx)

- **Build:** `west build -b nrf54l15dk/nrf54l15/cpuapp`
- Uses **nrfx** SPIM20 on P1.05 (SCK), P1.06 (MOSI), P1.09/1.14 (CS). Board overlay leaves `&spi20` with empty pinctrl so the Zephyr SPI driver does not configure pins; nrfx owns the hardware.

## Zephyr SPI sanity check

- **Build:**  
  `west build -b nrf54l15dk/nrf54l15/cpuapp -- -DCONF_FILE="prj.conf;prj_zephyr_spi.conf" -DOVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_zephyr_spi.overlay`
- Uses the **Zephyr SPI** driver on spi20 with pinctrl (same pins: P1.05 SCK, P1.06 MOSI). CS still manual on P1.09. One test transfer `0xAA, 0x55` after the P1 GPIO blink test.

## Revert to nrfx

- Build again **without** `-DCONF_FILE` and `-DOVERLAY_FILE` (or use a clean build with no extra args). Same as default above.
