# KincoDrive Control Module V5.4

**Actuation & IO Distribution Board — Embedded Firmware**

MCU: STM32G0B1RET6 | CAN: Extended (29-bit) | Device ID: `0x0667`

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

### Broadcast Messages (KincoDrive → Host)

Sent periodically (default 500 ms).

#### 0x600 — System Status (8 bytes)

| Byte | Signal | Encoding | Unit |
|------|--------|----------|------|
| 0 | Endstop triggered | 1 bit per channel (see below) | bitfield |
| 1 | Endstop fault | 1 bit per channel | bitfield |
| 2–3 | 24V bus voltage | uint16 LE | 0.1V |
| 4 | 12V bus voltage | uint8 | 0.1V |
| 5 | Protection state | bitfield (see below) | — |
| 6–7 | Reserved | 0x00 | — |

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

---

### Command Messages (Host → KincoDrive)

#### 0x060 — Toggle Test LED (1 byte)

| Byte | Description |
|------|-------------|
| 0 | Any value (triggers LED toggle) |

**ACK**: `0x700` → `[0x60, 0x00]`

#### 0x110 — Set Drive High-Side Power (1 byte)

| Byte | Description |
|------|-------------|
| 0 | 0x00 = disable, non-zero = enable |

**ACK**: `0x700` → `[msg_type, 0x00]`

#### 0x111 — Set Extruder High-Side Power (1 byte)

Same payload format as 0x110.

#### 0x112 — Set Scrubbing High-Side Power (1 byte)

Same payload format as 0x110.

#### 0x113 — Enable 12V Buck Converter (1 byte)

| Byte | Description |
|------|-------------|
| 0 | 0x00 = disable, non-zero = enable |

Same payload format as 0x110. Controls the 12V buck converter (PD8). Only enables if 24V bus is above minimum threshold.

> **Note**: Defined in protocol but handler not yet registered — reserved for future use.

#### 0x140 — Set Fan PWM (2 bytes)

| Byte | Description |
|------|-------------|
| 0 | Fan selector: 1=DR, 2=EP, 3=EH, 4=ST, 5=SF |
| 1 | Speed: 0–100 (%) |

#### 0x200 — Write EEPROM Config Setting (3 bytes)

| Byte | Description |
|------|-------------|
| 0 | Setting selector (see table below) |
| 1–2 | New value (uint16 LE) |

| Selector | Setting | Unit | Range |
|----------|---------|------|-------|
| 0x00 | Hard overvoltage threshold | mV | 10000–60000 |
| 0x01 | Soft overvoltage threshold | mV | 10000–60000 |
| 0x02 | Under-voltage threshold | mV | 5000–30000 |
| 0x03 | Overcurrent threshold | mA | 500–60000 |
| 0x04 | Fan max RPM | RPM | 100–30000 |

**ACK on 0x604**: echoes `[selector, value_lo, value_hi]`

Constraint: `under_voltage < soft_OV < hard_OV`

#### 0x201 — Read EEPROM Config (0 bytes)

No payload required. Response:

**Frame 1 on 0x604** (8 bytes):

| Byte | Content | Encoding |
|------|---------|----------|
| 0–1 | hard_over_voltage | u16 LE mV |
| 2–3 | soft_over_voltage | u16 LE mV |
| 4–5 | over_current | u16 LE mA |
| 6–7 | fan_max_rpm | u16 LE RPM |

**Frame 2 on 0x605** (2 bytes):

| Byte | Content | Encoding |
|------|---------|----------|
| 0–1 | under_voltage | u16 LE mV |

#### 0x703 — Dump Error Log (0 bytes)

No payload required. Response on `0x701` (8 bytes per error):

| Byte | Content |
|------|---------|
| 0 | Error index (0-based, oldest first) |
| 1 | Error source code |
| 2 | Severity (0=warning, 1=critical) |
| 3–4 | Detail (uint16 LE) |
| 5–7 | Timestamp lower 3 bytes (ms, LE) |

Terminator frame: all bytes = `0xFF`.

**Error source codes**:

| Code | Source |
|------|--------|
| 0x00 | None |
| 0x01 | Overcurrent — Drive |
| 0x02 | Overcurrent — Extruder |
| 0x03 | Overcurrent — Scrubbing |
| 0x04 | Overcurrent — 24V Bus |
| 0x10 | Overvoltage — Soft |
| 0x11 | Overvoltage — Hard |
| 0x12 | Undervoltage |
| 0x20 | Endstop Fault |
| 0x21 | ESTOP Fault |
| 0x22 | ESTOP Triggered |
| 0x30 | Thermal Fault |
| 0x40 | EEPROM Fault |
| 0x50 | CAN Bus Fault |

#### 0x704 — Reset Error State (1 byte)

| Byte | Description |
|------|-------------|
| 0 | Safety key: must be `0xAA` |

**Response on 0x702**:

| Status | Meaning |
|--------|---------|
| `0xBB` | Entered RECOVERY state |
| `0xAA` | Recovery complete → NORMAL |
| `0xEE` | Recovery failed (overcurrent or timeout) |
| `0xCC` | Recovery failed (overvoltage still present) |

---

### Bootloader Entry

Send to raw CAN ID `0x0667` (the device ID itself) with `payload[0] = 0xFF`.
The device immediately performs a system reset for bootloader handover.

---

## System State Machine

```
                          ┌──────────────┐
     Power-on ───────────►│   NORMAL     │
                          │              │◄──── recovery success
                          └──────┬───────┘
                                 │ soft OV
                          ┌──────▼───────┐
                          │   WARNING    │
                          │  (no block)  │
                          └──────┬───────┘
                                 │ critical fault
                          ┌──────▼───────┐
                          │    ERROR     │◄──── recovery timeout / re-fault
                          │  (lockout)   │
                          └──────┬───────┘
                                 │ CAN reset (0xAA key)
                          ┌──────▼───────┐
                          │  RECOVERY    │
                          │ (validating) │
                          └──────────────┘
```

- **NORMAL/WARNING**: all operations allowed
- **ERROR**: affected modules locked off, error log active
- **RECOVERY**: modules stay disabled while readings are validated; requires N consecutive good readings before returning to NORMAL

---

## Module Overview

| Module | Files | Purpose |
|--------|-------|---------|
| CAN Handler | `CAN_Handler.c/h` | Message dispatch, ring buffer, broadcast engine |
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

Key pin groups:

- **High-side switches**: `HS_DR_EN`, `HS_E_EN`, `HS_SC_EN` (enable), `HS_DR_PG`, `HS_E_PG`, `HS_SC_PG` (power good), `HS_DR_FT`, `HS_E_FT`, `HS_SC_FT` (fault)
- **Fan PWM**: TIM1 CH1–4 + TIM14 CH1
- **Fan Tacho**: TIM2 CH1–4 + TIM3 CH2
- **ADC**: PA0–PA7, PB0–PB2, PB10, PB12 (thermistors, current sense, voltage dividers)
- **FDCAN**: PB8 (RX), PB9 (TX)
- **ESTOP**: PB13 (NO), PB14 (NC), PB15 (LED)
- **12V Buck**: PD8 (VBUCK_CTRL)
