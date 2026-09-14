cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(wire-sensor)

target_sources(app PRIVATE
  src/main.c
  src/app_state.c
  src/app_events.c
  src/app_ui.c
  src/app_controller.c
  src/app_identity.c
  src/sensor.c
  src/battery.c
  src/ble.c
  src/led_status.c
  src/nfc_pairing.c
  src/session_crypto.c
)
