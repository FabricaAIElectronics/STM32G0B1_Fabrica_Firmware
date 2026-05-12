# KincoDrive Control Module V5.4

**Actuation & IO Distribution Board — Embedded Firmware (low-level driver build)**

MCU: STM32G0B1RET6 | CAN: 11-bit standard, sub-block `0x101-0x12F` | Bootloader RX `0x101` / TX `0x102`

This build is intentionally minimal: state machine just runs the low-level drivers, broadcasts telemetry, and applies CAN commands. No high-level application logic is layered on yet — the goal is to validate the hardware and the firmware drivers.

**Protections active**: overcurrent (per high-side channel) and undervoltage (24 V and 12 V buses). Nothing else — no ESTOP, no endstop wiring-fault auto-shutdown, no PG-missing auto-shutdown.

---

## Build & Flash

- **IDE**: STM32CubeIDE
- **Toolchain**: arm-none-eabi-gcc
- **Config**: `Actuation_IO_Distribution_Board_Embedded.ioc`

Open the `.ioc` in STM32CubeIDE, generate code (this regenerates `main.c` GPIO_Init + `main.h` pin defines), then build. After the refactor, `main.h` may still contain stale `EStop_*`, `Endstop_*`, and `EneStop_*` pin defines from a previous .ioc — re-running CubeMX with the deleted pins removed from the .ioc will clean these up.

The application sits behind the OpenBLT bootloader at `STM32G0B1_Bootloader/G0B1_KincoDrive_Boot/`.

---

## Module Overview

| Module | Files | Purpose |
|--------|-------|---------|
| Application logic | `applogic.c/h` | INIT → LOAD_CONFIG → RUNNING state machine |
| CAN handler | `CAN_Handler.c/h` | Command dispatch + 8 telemetry broadcasts (3-phase) |
| ADC driver | `adc_driver.c/h` | DMA buffer + calibration + `adc_to_mV()` helper |
| HS switch | `hs_switch.c/h` | Drive/Extruder/Scrubbing EN + 12 V buck — pure GPIO toggle |
| Power monitor | `power_monitor.c/h` | Voltage/current reads, OC trip + UV detect |
| Thermistor | `thermistor.c/h` | 6× PTC reads (Beta equation, status only) |
| Fan PWM | `Fan_PWM.c/h` | 5-channel PWM + tachometer DMA |
| EEPROM driver | `eeprom_driver.c/h` | I2C EEPROM 24-byte config (HS state, fans, OC + UV thresholds) |

---

## CAN Protocol

### ID Layout

11-bit standard, all in `0x101-0x12F` (CiA 301 reserved gap between TIME and TPDO1, CANopen-coexistence safe).

**DBC files**: `KincoDrive_ControlModule.dbc` (this device only) or `Docs/Fabrica_Bus.dbc` (combined bus map). Open with Vector CANdb++ Editor or PCAN-Explorer.

### Commands (Host → Device)

| ID | Name | DLC | Payload |
|----|------|-----|---------|
| `0x101` | Bootloader RX / Reset | 2 | `0xFF 0x00` → reset into OpenBLT (also XCP CONNECT) |
| `0x110` | Cmd_HS_Power | 1 | bitmask: bit0=Drive bit1=Extruder bit2=Scrubbing bit3=12V_Buck. Setting an HS bit also clears any latched OC error for that channel |
| `0x111` | Cmd_Fan_PWM | 5 | 0–100 % per fan: byte0=DR byte1=EP byte2=EH byte3=ST byte4=SF |
| `0x112` | Cmd_EEPROM | 1 | byte0: 0=load defaults+apply, 1=snapshot current state to EEPROM |
| `0x113` | Cmd_OC_Threshold | 6 | uint16 LE × 3 in mA: byte0..1=DR, byte2..3=EXT, byte4..5=SC |
| `0x114` | Cmd_UV_Threshold | 4 | uint16 LE × 2 in mV: byte0..1=V24, byte2..3=V12 |

### Broadcasts (Device → Host, ~167 ms each in 3-phase rotation = 500 ms cycle)

| ID | Name | DLC | Notes |
|----|------|-----|-------|
| `0x120` | Bcast_Status | 8 | V24 mV, V12 mV, I_24V mA, sys_state, error_mask |
| `0x121` | Bcast_Currents | 8 | bus_24V + 3× HS currents (uint16 LE mA each) |
| `0x122` | Bcast_Temps | 6 | 6× PTC temp (int8, +40 offset) |
| `0x123` | Bcast_Fans | 5 | 5× fan tachometer % |
| `0x124` | Bcast_GPIO | 8 | Full raw GPIO state (no interpretation) |
| `0x125` | Bcast_Raw_ADC | 6 | 12 ADC channels packed as 4-bit nibbles (raw>>8) |
| `0x126` | Bcast_Config_A | 8 | EEPROM HS_state + 3× OC thresholds |
| `0x127` | Bcast_Config_B | 8 | EEPROM UV thresholds + fan boot defaults |

### Bcast_Status (0x120) byte layout

| Byte | Signal | Encoding |
|---|---|---|
| 0–1 | V24_mV | uint16 LE |
| 2–3 | V12_mV | uint16 LE |
| 4–5 | I_24V_mA | uint16 LE |
| 6 | Sys_State | 0=INIT, 1=LOAD_CONFIG, 2=RUNNING |
| 7 | Error_Mask | bit0=OC_DR, bit1=OC_EXT, bit2=OC_SC, bit3=UV_24V, bit4=UV_12V |

### Bcast_GPIO (0x124) byte layout

All bits are raw pin state with no debounce. PG/FT bits are pre-inverted so that **1 = asserted**.

| Byte | Bit | Signal |
|---|---|---|
| 0 | 0 | HS_DR_Enable (EN GPIO HIGH) |
| 0 | 1 | HS_E_Enable |
| 0 | 2 | HS_SC_Enable |
| 0 | 3 | VBUCK_Enable |
| 1 | 0 | HS_DR_PwrGood (1 = TPS2493 PGOOD asserted) |
| 1 | 1 | HS_E_PwrGood |
| 1 | 2 | HS_SC_PwrGood |
| 2 | 0 | HS_DR_Fault (1 = TPS2493 FAULT asserted) |
| 2 | 1 | HS_E_Fault |
| 2 | 2 | HS_SC_Fault |
| 3 | 0 | B1_Pin (PC13) |
| 3 | 1 | Toggle_Pos_Detect (PC0) |
| 4 | 0 | LED2_Out (PA5 heartbeat) |

---

## Protection model

### Overcurrent (OC)

Configured per HS channel (default 5 A). The 50 ms protection tick:

1. For each enabled HS channel, reads instantaneous current.
2. If `current > threshold`, **disables the EN GPIO** and sets the corresponding ERR_OC bit.
3. The ERR_OC bit **stays set** until the host sends a Cmd_HS_Power with that channel's bit asserted — that command auto-clears the OC error and re-enables the EN GPIO. If the OC condition is still present, the next protection tick will trip again.

### Undervoltage (UV)

Configured per bus (default 20 V for 24 V bus, 10 V for 12 V bus). The 50 ms protection tick:

1. If `bus < threshold`, sets the corresponding ERR_UV bit. **Does not auto-disable any HS channel** — UV is a status flag, the host decides.
2. ERR_UV bit auto-clears when the bus rises **500 mV above the threshold** (hysteresis).

---

## EEPROM startup config

24-byte struct stored at page 0, offset 0:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic (`0xA6`) |
| 1 | 1 | hs_state bitmask |
| 2 | 5 | fan_dr/ep/eh/st/sf default % |
| 7 | 6 | oc_dr/e/sc thresholds (uint16 LE mA) |
| 13 | 4 | uv_24V/uv_12V thresholds (uint16 LE mV) |
| 17 | 6 | reserved |
| 23 | 1 | XOR checksum of bytes 0..22 |

If magic or checksum is invalid at boot, safe defaults (all HS OFF, fans 0 %, OC=5000 mA, UV=20000/10000 mV) are loaded into the RAM cache. They are **not** written to EEPROM until the host explicitly issues Cmd_EEPROM byte0=1.

| Cmd_EEPROM byte0 | Effect |
|---|---|
| `0x00` | Load safe defaults into RAM cache, apply to HW |
| `0x01` | Snapshot current HS state + fan setpoints + thresholds → EEPROM |

After any save, Bcast_Config_A and Bcast_Config_B reflect the new EEPROM contents on their next cycle.

---

## Bootloader entry

Send to CAN ID `0x101` (DLC=2) with `payload[0] = 0xFF` → immediate system reset into the OpenBLT bootloader. The bootloader then re-handles the same XCP CONNECT frame on `0x101` and replies on `0x102`. Host therefore needs only one ID pair (`0x101 / 0x102`) to flash the device.

---

## Heartbeat LED

PA5 (LED2) toggles every ~1 second through the normal broadcast cycle (visible by clock skew on `Bcast_GPIO` byte 4 bit 0). If it stops toggling, the MCU is stuck in `Error_Handler()`.

---

## Bench validation checklist

1. Build cleanly in STM32CubeIDE with no unresolved symbols.
2. Power on with no HS load — Bcast_GPIO byte 0 reflects EEPROM hs_state, byte 1 PGOOD bits track the TPS2493s.
3. Send Cmd_HS_Power 0x01 → HS_DR_EN goes HIGH on Bcast_GPIO; current draw appears in Bcast_Currents.
4. Short the Drive rail to ground briefly → Bcast_Status byte 7 shows `0x01` (ERR_OC_DRIVE) and Bcast_GPIO byte 0 bit 0 returns to 0.
5. Send Cmd_HS_Power 0x01 again → channel re-enables; if short still present, it re-trips on the next 50 ms cycle.
6. Drop the 24 V supply below 20 V → Bcast_Status byte 7 shows `0x08` (ERR_UV_24V); raise above 20.5 V → bit clears.
7. Send Cmd_OC_Threshold (0x113) with new mA values → Bcast_Config_A reflects the change immediately; Cmd_EEPROM 0x01 → power cycle → values persist.
