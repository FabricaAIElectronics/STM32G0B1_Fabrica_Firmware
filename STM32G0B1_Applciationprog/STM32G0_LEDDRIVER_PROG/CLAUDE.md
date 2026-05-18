# STM32G0 Embedded Projects — Session Notes

## Overview

Two STM32G0B1-based embedded firmware projects sharing a common architecture (OpenBLT bootloader, HAL drivers, EEPROM config, CAN bus). Both target custom PCBs with different application domains.

---

## Project 1: STM32G0_LEDDRIVER_PROG (LED Driver)

**Purpose:** Controls 3-channel PWM LED output with voltage monitoring and EEPROM-stored configuration.

**MCU:** STM32G0B1 — single FDCAN bus (canHandle)

**Key modules:**

- `applogic.c/h` — State machine: INIT → LOAD_CONFIG → RUNNING → ERROR → RECOVERY. Handles undervoltage debounce (24V, 17.5V) and timed ADC/CAN status broadcasts.
- `can_operation.c/h` — 11-bit standard CAN IDs (0x160-0x17A, CANopen-safe gap). Commands: LIGHTSET (PWM), VOLTAGESET (UV thresholds), EEPROMSET, DEVICEID. Broadcasts: LIGHTSTATUS, DEVSTATUS, EEPROMDATA.
- `peripheral.c/h` — PWM output (3ch), ADC voltage reading (24V, 17.5V), LED enable/disable via GPIO.
- `eeprom_driver.c/h` — I2C EEPROM config persistence (page-based read/write).
- LED heartbeat is inline in `main.c` (HAL_GPIO_TogglePin on LED_GREEN every 500 ms).
- Bootloader-from-app trigger is in `can_operation.c` ISR — XCP CONNECT (byte0=0xFF, dlc=2) on DEVICEID (0x160) calls NVIC_SystemReset(). No App/ folder; no OpenBLT BootCom hooks linked into the app.

**CAN IDs (29-bit extended):**

| ID | Name | Direction |
|---|---|---|
| 0x160 | DEVICEID (also bootloader RX) | RX |
| 0x161 | Bootloader TX | TX |
| 0x170 | LIGHTSET | RX |
| 0x171 | VOLTAGESET | RX |
| 0x172 | EEPROMSET | RX |
| 0x178 | EEPROMDATA | TX |
| 0x179 | LIGHTSTATUS | TX |
| 0x17A | DEVSTATUS | TX |

---

## Project 2: PowerStage (Power Distribution Controller)

**Purpose:** Manages 5 hot-swap power rails (TPS2493), fan control, current/voltage monitoring, with OLED display and dual CAN bus gateway.

**MCU:** STM32G0B1 — dual FDCAN (FDCAN1 internal + FDCAN2 host relay)

**Key modules:**

- `powerstage_app.c/h` — State machine: APP_STATE_INIT → APP_STATE_RUNNING → APP_STATE_FAULT. Orchestrates all subsystems with 500ms broadcast cycle.
- `can_operation.c/h` — 17 CAN messages (0x130–0x159), 11-bit standard IDs in the CANopen-safe gap. Dual-bus relay: CAN2 RX→CAN1 TX and vice versa (any frame format), all broadcasts on both buses via `CAN_SendAll()`.
- `io_module.c/h` — 5 power rails (AUX, LED, DRIVE, CAP, SBC) with TPS2493 hot-swap controllers. 10-channel ADC (6 currents + 3 voltages + NTC). Current sensing: 3mΩ shunt (most rails), 20mΩ (LED rail). K_SCALE calibrated = 1.250e-2.
- `fan_ctrl.c/h` — PWM fan with 3 modes: OFF, ON_MANUAL, AUTO (temp hysteresis). Configurable duty, min duty, on/off temps.
- `eeprom_driver.c/h` — 24-byte config at I2C 0xA0. Stores fan defaults, HS default state, OC thresholds.
- `ssd1306.c/h` — SSD1309 OLED driver (I2C). Display scheduler shows measurements, status, animations.
- `display_scheduler.c/h` — Rotating OLED pages (voltages, currents, fan, status).
- `ui_display.c/h` — UI rendering helpers, fonts, bitmaps, running horse animation.

**Power rails:**

| Index | Name | Shunt | Notes |
|---|---|---|---|
| 0 | RAIL_AUX | 3mΩ | Auxiliary supply |
| 1 | RAIL_LED | 20mΩ | LED power |
| 2 | RAIL_DRIVE | 3mΩ | Motor/drive |
| 3 | RAIL_CAP | 3mΩ | Capacitor bank |
| 4 | RAIL_SBC | 3mΩ | Single board computer |

**CAN protocol (11-bit standard IDs):**

| ID | Name | Dir | DLC | Description |
|---|---|---|---|---|
| 0x130 | DEVICE_ADDR / Bootloader RX | RX | 2 | System reset / bootloader trigger |
| 0x131 | Bootloader TX | TX | - | OpenBLT XCP responses |
| 0x140 | CMD_FAN | RX | 2 | Fan mode + duty cycle |
| 0x141 | CMD_HS | RX | 5 | Per-rail enable/disable |
| 0x142 | CMD_OC | RX | 4 | Overcurrent threshold / fault reset |
| 0x143 | CMD_EEPROM | RX | 1 | Save config / load defaults |
| 0x144 | CMD_UV | RX | 6 | Undervoltage thresholds (V24, VCAP, V12) |
| 0x145 | CMD_CTRL | RX | 2 | LED power + relay on/off |
| 0x150 | BCAST_HS_STATE | TX | 5 | Enable/fault/pgood/OC bitmasks |
| 0x151 | BCAST_HS_CURR_A | TX | 8 | BAT, CAP, SBC, DRIVE currents (mA) |
| 0x152 | BCAST_VOLTAGE | TX | 8 | V24, VCAP, V12 (mV) + UV fault mask + 6S battery SOC % (byte 7) |
| 0x153 | BCAST_FAN | TX | 4 | Fan mode, duty, temperature |
| 0x154 | BCAST_EEPROM | TX | 8 | Config echo (fan + HS defaults) |
| 0x155 | BCAST_HS_CURR_B | TX | 4 | AUX, LED currents (mA) |
| 0x156 | BCAST_UV | TX | 6 | Active UV thresholds echo |
| 0x157 | BCAST_OC_CFG_A | TX | 8 | OC thresholds (AUX, LED, DRIVE, CAP) |
| 0x158 | BCAST_OC_CFG_B | TX | 2 | OC threshold (SBC) |
| 0x159 | BCAST_IO_STATUS | TX | 3 | Switch pins, LED power, relay status |

**Dual CAN bus architecture:**

- FDCAN1 (PB8/PB9) — Internal bus, primary for bootloader + application
- FDCAN2 (PC2/PC3) — Host bus, relay gateway for PC test tools
- Bidirectional transparent relay with no filtering
- All broadcasts sent on both buses simultaneously

**Documentation:**

- `Docs/CAN_Protocol.md` — Full CAN protocol reference (message layouts, encoding, examples)
- `PowerStage.dbc` — CAN database file for PCAN/Vector tools

---

## Shared Architecture

Both projects follow the same firmware pattern:

1. **Bootloader:** OpenBLT lives in `STM32G0B1_Bootloader/G0B1_LEDDriver_Boot/` (separate project). The application reaches it via NVIC_SystemReset triggered from the CAN ISR; no OpenBLT BootCom code is linked into the app build.
2. **HAL:** STM32 HAL drivers generated by CubeMX
3. **EEPROM:** I2C EEPROM driver for persistent configuration
4. **CAN:** FDCAN peripheral with ISR-driven RX parsing and periodic TX broadcasts
5. **State machine:** Init → Running → Error/Fault with recovery logic
6. **Timers:** Non-blocking tick-based scheduling (no RTOS)

---

## Session Work Log

### What was done (April 2026 session):

1. **Reviewed** existing PowerStage codebase for test readiness
2. **Implemented** dual CAN bus support — CAN2 host bus init, bidirectional relay, `CAN_SendAll()` for broadcasting on both buses
3. **Created CAN command handlers** — Fan control (CMD_FAN), hot-swap enable/disable (CMD_HS), overcurrent config (CMD_OC), undervoltage thresholds (CMD_UV), EEPROM save/load (CMD_EEPROM), relay/LED power control (CMD_CTRL)
4. **Created CAN broadcast functions** — 10 periodic broadcast messages covering rail state, currents, voltages, fan, EEPROM config, OC/UV thresholds, IO status
5. **Wrote CAN protocol documentation** — Comprehensive `Docs/CAN_Protocol.md` with all 16 message definitions, encoding conventions, byte layouts, and test examples
6. **Generated DBC file** — `PowerStage.dbc` for use with PCAN Explorer / Vector CANalyzer

---

## Build Notes

- Toolchain: ARM GCC (STM32CubeIDE)
- CubeMX project files present in both repos
- No RTOS — bare-metal with cooperative main-loop scheduling
- Both 500 kbps nominal CAN baud rate, classic CAN (no FD)
