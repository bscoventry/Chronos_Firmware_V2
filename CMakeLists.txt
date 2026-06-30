cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(Chronos_nRF54_2)

target_sources(app PRIVATE
  src/main.c
  src/BLE.c
  src/data.c
  src/rtc_stim.c
  src/spi.c
  src/stim_nvm.c
  src/timer.c
)

if(CONFIG_BT)
  target_sources(app PRIVATE src/ble_session.c)
endif()

if(CONFIG_CHRONOS_ZMEAS)
  target_sources(app PRIVATE
    src/zmeas_gpio.c
    src/ad5933.c
  )
endif()
