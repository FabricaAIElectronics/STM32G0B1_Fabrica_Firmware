# KincoDrive Control Module V5.4 — TEST MODE

**Actuation & IO Distribution Board — Embedded Firmware (Broadcast-Only Prototype)**

MCU: STM32G0B1RET6 | CAN: Extended (29-bit) | Device ID: `0x0667`

> **TEST MODE**: No host commands accepted. The device broadcasts all sensor,
> GPIO, and ADC data automatically on power-up. Connect a CAN analyzer to
> observe system status without sending any commands.

---

## Build & Flash

- **IDE**: STM32CubeIDE
- **Toolchain**: arm-none-eabi-gcc
- **Config**: `Actuation_IO_Distribution_Board_Embedded.ioc`

Open the `.ioc` in STM32CubeIDE, generate code, then build the project.

---

## CAN Protocol

### ID Layout (29-bit Extended)

```
  Bits [28:16]    Bits [15:0]
 ┌─────────────┬───────────────┐
 │  Message     │  Device ID    │
 │  Type        │  (0x0667)     │
 └─────────────┴───────────────┘
```

**Formula**: `CAN_ID = (message_type << 16) | 0x0667`

**Example**: Broadcast Status `0x600` → Extended CAN ID = `0x06000667`

### CAN Database File

The file `KincoDrive_ControlModule.dbc` contains the full CAN database. Open with **Vector CANdb++ Editor** to browse messages, signals, and value tables interactively.

---

### Broadcast Messages (6 frames every 500 ms)

All messages are sent automatically on power-up. No host commands required.

#### 0x600 — System Status (8 bytes)

| Byte | Signal | Encoding | Unit |
|------|--------|----------|------|
| 0 | Endstop triggered | 1 bit per channel (see below) | bitfield |
| 1 | Endstop fault | 1 bit per channel | bitfield |
| 2–3 | 24V bus voltage | uint16 LE | 0.1V |
| 4 | 12V bus voltage | uint8 | 0.1V |
| 5 | Protection state | bitfield (see below) | — |
| 6 | System state | uint8 | enum |
| 7 | Error count | uint8 | count |

**Endstop bits** (same layout for triggered and fault bytes):

| Bit | Channel |
|-----|---------|
| 0 | Extruder Height Top (NC+NO) |
| 1 | Extruder Height Bottom (NO only) |
| 2 | Extruder Platform Top (NC+NO) |
| 3 | Extruder Platform Bottom (NC+NO) |
| 4 | Scrubbing Front Top (NC+NO) |
| 5 | ESTOP (NC+NO) |
| 6–7 | Reserved |

**Protection state byte**:

| Bits | Field | Values |
|------|-------|--------|
| [1:0] | Drive overcurrent | 0=OK, 1=OC |
| [3:2] | Extruder overcurrent | 0=OK, 1=OC |
| [5:4] | Scrubbing overcurrent | 0=OK, 1=OC |
| [7:6] | Overvoltage | 0=OK, 1=Soft, 2=Hard |

**System state**: 0=NORMAL, 1=WARNING, 2=ERROR, 3=RECOVERY

#### 0x601 — Currents (5 bytes)

| Byte | Signal | Encoding | Unit |
|------|--------|----------|------|
| 0–1 | 24V bus current | uint16 LE | 0.1A |
| 2 | Drive module current | uint8 | 0.1A |
| 3 | Extruder module current | uint8 | 0.1A |
| 4 | Scrubbing module current | uint8 | 0.1A |

#### 0x602 — Temperatures (8 bytes)

| Byte | Signal | Encoding | Unit |
|------|--------|----------|------|
| 0 | PTC 1 temperature | uint8, offset +40 | degC |
| 1 | PTC 2 temperature | uint8, offset +40 | degC |
| 2 | PTC 3 temperature | uint8, offset +40 | degC |
| 3 | PTC 4 temperature | uint8, offset +40 | degC |
| 4 | PTC 5 temperature | uint8, offset +40 | degC |
| 5 | PTC 6 temperature | uint8, offset +40 | degC |
| 6–7 | Reserved | 0x00 | — |

Decode: `temp_degC = wire_value - 40` (range: -40 to 215 degC)

#### 0x603 — Fan Speeds (5 bytes)

| Byte | Signal | Encoding | Unit |
|------|--------|----------|------|
| 0 | Fan DR (Drive) | uint8 | % of max RPM |
| 1 | Fan EP (Extruder Platform) | uint8 | % of max RPM |
| 2 | Fan EH (Extruder Head) | uint8 | % of max RPM |
| 3 | Fan ST (Stepper) | uint8 | % of max RPM |
| 4 | Fan SF (Scrubbing Front) | uint8 | % of max RPM |

#### 0x604 — GPIO Status (8 bytes)

| Byte | Signal | Encoding |
|------|--------|----------|
| 0 | HS Enable pins | Bit0=DR_EN, Bit1=E_EN, Bit2=SC_EN, Bit3=VBUCK |
| 1 | HS Power-Good pins | Bit0=DR_PG, Bit1=E_PG, Bit2=SC_PG (1=good) |
| 2 | HS Fault pins | Bit0=DR_FT, Bit1=E_FT, Bit2=SC_FT (1=fault) |
| 3 | ESTOP raw | Bit0=NO_pin, Bit1=NC_pin, Bit2=debounced |
| 4 | Endstop raw byte 1 | See bit layout below |
| 5 | Endstop raw byte 2 | Bit0=SC_H_NC |
| 6 | LED state | 0=off, 1=on |
| 7 | Blue button | 0=released, 1=pressed |

**Endstop raw byte 1 (byte 4)**:

| Bit | Pin |
|-----|-----|
| 0 | EH_H_NO (Extruder Height Top NO) |
| 1 | EH_H_NC (Extruder Height Top NC) |
| 2 | EH_L_NO (Extruder Height Bottom NO) |
| 3 | EP_H_NO (Extruder Platform Top NO) |
| 4 | EP_H_NC (Extruder Platform Top NC) |
| 5 | EP_L_NO (Extruder Platform Bottom NO) |
| 6 | EP_L_NC (Extruder Platform Bottom NC) |
| 7 | SC_H_NO (Scrubbing Front Top NO) |

#### 0x605 — Raw ADC (6 bytes)

12 ADC channels packed as 4-bit nibbles (12-bit raw value >> 8). Each byte contains two channels: high nibble first, low nibble second.

| Byte | High nibble | Low nibble |
|------|-------------|------------|
| 0 | PTC 1 | PTC 2 |
| 1 | PTC 3 | PTC 4 |
| 2 | PTC 5 | PTC 6 |
| 3 | CURR_MON_1 (Drive) | CURR_MON_2 (Extruder) |
| 4 | CURR_MON_3 (Scrubbing) | VADC_24 (24V bus) |
| 5 | VADC_12 (12V bus) | CURR_MON_24V (24V bus current) |

Scale: `raw_12bit ≈ nibble × 256` (coarse indication for quick verification)

---

### Bootloader Entry

Send to raw CAN ID `0x0667` (the device ID itself) with `payload[0] = 0xFF`.
The device immediately performs a system reset for bootloader handover.

---

## Heartbeat LED

PA5 toggles every 1 second as a heartbeat indicator. If the LED is not blinking, the MCU is stuck (likely in `Error_Handler()`).

---

## Module Overview

| Module | Files | Purpose |
|--------|-------|---------|
| CAN Handler | `CAN_Handler.c/h` | Broadcast engine (test mode, no command handlers) |
| Power Electronic | `Power_Electronic.c/h` | High-side switch control, ADC sensing, protection |
| Fan PWM | `Fan_PWM.c/h` | 5-channel PWM output + tachometer DMA |
| Endstop | `Endstop.c/h` | 5 endstop switches (NC+NO / NO-only) |
| ESTOP | `ESTOP.c/h` | Emergency stop with debounce state machine |
| EEPROM Driver | `eeprom_driver.c/h` | I2C EEPROM config storage with cache |
| Error Manager | `error_manager.c/h` | Error logging, state machine, recovery |
| Core System | `Core_System.c`, `Core_Systems.h` | Top-level main-loop state router |
| FDCAN | `fdcan.c/h` | Low-level FDCAN peripheral + TX helper |

---

## Pin Assignments

See `main.h` for the full GPIO pin mapping generated by STM32CubeMX.

Key pin groups: **High-side switches** (`HS_DR_EN`, `HS_E_EN`, `HS_SC_EN` enable; `_PG` power good; `_FT` fault), **Fan PWM** (TIM1 CH1–4 + TIM14 CH1), **Fan Tacho** (TIM2 CH1–4 + TIM3 CH2), **ADC** (PA0–PA7, PB0–PB2, PB10, PB12), **FDCAN** (PB8 RX, PB9 TX), **ESTOP** (PB13 NO, PB14 NC, PB15 LED), **12V Buck** (PD8).
