# KincoDrive Control Module V5.4

**Actuation & IO Distribution Board — Embedded Firmware (Prototype)**

MCU: STM32G0B1RET6 | CAN: Extended (29-bit) | Device ID: `0x0667`

---

## Build & Flash

- **IDE**: STM32CubeIDE
- **Toolchain**: arm-none-eabi-gcc
- **Config**: `Actuation_IO_Distribution_Board_Embedded.ioc`

Open the `.ioc` in STM32CubeIDE, generate code, then build.

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

**DBC file**: `KincoDrive_ControlModule.dbc` — open with **Vector CANdb++ Editor**.

---

### Commands (Host → Device)

| Msg Type | Name | DLC | Payload | Description |
|----------|------|-----|---------|-------------|
| 0x110 | Cmd_HS_Power | 1 | Byte 0 bitmask: bit0=Drive, bit1=Extruder, bit2=Scrubbing, bit3=12V Buck (0=OFF, 1=ON) | Set all 4 HS channels in one frame |
| 0x140 | Cmd_Fan_PWM | 5 | Bytes 0–4 = DR/EP/EH/ST/SF speed (0–100 %) | Set all 5 fans in one frame |
| 0x200 | Cmd_EEPROM | 1 | Byte 0 bitmask: bit0=Load defaults from EEPROM, bit1=Save current state to EEPROM | EEPROM startup config control |

**Fan order** (bytes 0–4): Drive, Ext Platform, Ext Head, Stepper, Scrub Front

---

### Broadcasts (Device → Host, every 500 ms)

#### 0x600 — Bcast_Status (8 bytes)

| Byte | Signal | Encoding |
|------|--------|----------|
| 0 | Endstop triggered | bitfield (bit0=EH_Top, bit1=EH_Bot, bit2=EP_Top, bit3=EP_Bot, bit4=SC_Top, bit5=ESTOP) |
| 1 | Endstop fault | same layout, 1=wiring fault |
| 2–3 | Bus24V_Voltage | uint16 LE, 0.1V/bit |
| 4 | Bus12V_Voltage | uint8, 0.1V/bit |
| 5 | Dbg_Cmd_Count | wrapping counter, +1 per dispatched command |
| 6–7 | Dbg_Last_MsgType | uint16 LE, last message type processed |

#### 0x601 — Bcast_Currents (5 bytes)

| Byte | Signal | Encoding |
|------|--------|----------|
| 0–1 | Bus24V_Current | uint16 LE, 0.1A/bit |
| 2 | Drive_Current | uint8, 0.1A/bit |
| 3 | Extruder_Current | uint8, 0.1A/bit |
| 4 | Scrubbing_Current | uint8, 0.1A/bit |

#### 0x602 — Bcast_Temps (6 bytes)

| Byte | Signal | Encoding |
|------|--------|----------|
| 0–5 | Temp_PTC1–PTC6 | uint8, offset +40 (degC = value − 40) |

#### 0x603 — Bcast_Fans (5 bytes)

| Byte | Signal | Encoding |
|------|--------|----------|
| 0–4 | Fan DR/EP/EH/ST/SF speed | uint8, % of 5000 RPM max |

#### 0x604 — Bcast_GPIO (8 bytes)

Individual 1-bit signals per pin — no bit-masking needed in the CAN analyzer.

| Byte | Bit | Signal | Note |
|------|-----|--------|------|
| 0 | 0 | HS_DR_Enable | 1=EN pin HIGH |
| 0 | 1 | HS_E_Enable | 1=EN pin HIGH |
| 0 | 2 | HS_SC_Enable | 1=EN pin HIGH |
| 0 | 3 | VBUCK_Enable | 1=CTRL pin HIGH |
| 1 | 0 | HS_DR_PwrGood | Active LOW inverted: 1=good |
| 1 | 1 | HS_E_PwrGood | Active LOW inverted: 1=good |
| 1 | 2 | HS_SC_PwrGood | Active LOW inverted: 1=good |
| 2 | 0 | HS_DR_Fault | Active LOW inverted: 1=fault |
| 2 | 1 | HS_E_Fault | Active LOW inverted: 1=fault |
| 2 | 2 | HS_SC_Fault | Active LOW inverted: 1=fault |
| 3 | 0 | ESTOP_NO_Pin | Raw NO pin: 1=HIGH |
| 3 | 1 | ESTOP_NC_Pin | Raw NC pin: 1=HIGH |
| 3 | 2 | ESTOP_Triggered | Debounced: 1=triggered |
| 3 | 3 | ESTOP_WiringFault | 1=NO/NC inconsistent |
| 4 | 0 | EH_Top_NO | Extruder Head top, NO: 1=HIGH |
| 4 | 1 | EH_Bot_NO | Extruder Head bot, NO: 1=HIGH (NO-only) |
| 4 | 2 | EP_Top_NO | Extruder Platform top, NO: 1=HIGH |
| 4 | 3 | EP_Bot_NO | Extruder Platform bot, NO: 1=HIGH |
| 4 | 4 | SC_Top_NO | Scrubbing top, NO: 1=HIGH |
| 5 | 0 | EH_Top_NC | Extruder Head top, NC: 1=HIGH |
| 5 | 2 | EP_Top_NC | Extruder Platform top, NC: 1=HIGH |
| 5 | 3 | EP_Bot_NC | Extruder Platform bot, NC: 1=HIGH |
| 5 | 4 | SC_Top_NC | Scrubbing top, NC: 1=HIGH |
| 6 | 0 | LED_Out | 1=HIGH |
| 6 | 1 | BlueButton | 1=HIGH |
| 7 | 0–7 | Dbg_ISR_Count | Wrapping counter, +1 per received CAN frame |

NC+NO health: NC=1 & NO=0 → OK, NC=0 & NO=1 → triggered, else → wiring fault.
EH_Bot is NO-only (no NC contact).

#### 0x605 — Bcast_Raw_ADC (6 bytes)

12 ADC channels as 4-bit nibbles (12-bit → top 4 bits). High nibble first.

| Byte | Hi nibble | Lo nibble |
|------|-----------|-----------|
| 0 | PTC1 | PTC2 |
| 1 | PTC3 | PTC4 |
| 2 | PTC5 | PTC6 |
| 3 | CURR_DR | CURR_EXT |
| 4 | CURR_SC | V_24V |
| 5 | V_12V | CURR_24V |

#### 0x606 — Bcast_Config (6 bytes)

EEPROM startup defaults — sent every 500 ms and immediately after any `Cmd_EEPROM`.

| Byte | Bit(s) | Signal | Encoding |
|------|--------|--------|----------|
| 0 | 0 | Def_HS_DR | 1=Drive ON at boot |
| 0 | 1 | Def_HS_E | 1=Extruder ON at boot |
| 0 | 2 | Def_HS_SC | 1=Scrubbing ON at boot |
| 0 | 3 | Def_VBUCK | 1=12V Buck ON at boot |
| 1 | 0–7 | Def_Fan_DR | Drive fan default % |
| 2 | 0–7 | Def_Fan_EP | Extruder Platform fan default % |
| 3 | 0–7 | Def_Fan_EH | Extruder Head fan default % |
| 4 | 0–7 | Def_Fan_ST | Stepper fan default % |
| 5 | 0–7 | Def_Fan_SF | Scrubbing Front fan default % |

---

### EEPROM Startup Config

The device stores an 8-byte config in EEPROM (page 0): magic byte (0xA5), HS default state bitmask, 5 fan default speeds, and an XOR checksum.

On power-up, `EEPROM_Init()` reads the config. If the magic or checksum is invalid, safe defaults are used (all HS OFF, all fans 0%) without writing to EEPROM. `EEPROM_ApplyStartupConfig()` is then called to apply the config to hardware.

| Command byte | Effect |
|---|---|
| `0x01` (bit0) | Load: re-apply cached config to hardware immediately |
| `0x02` (bit1) | Save: snapshot current GPIO/fan state to EEPROM |
| `0x03` (both) | Save then respond with current config |

After any EEPROM command, `Bcast_Config` (0x606) is sent immediately.

---

### Bootloader Entry

Send to raw CAN ID `0x0667` (no message type prefix) with `payload[0] = 0xFF` → immediate system reset into bootloader.

---

## Heartbeat LED

PA5 toggles every 1 second. If not blinking, the MCU is stuck in `Error_Handler()`.

---

## Module Overview

| Module | Files | Purpose |
|--------|-------|---------|
| CAN Handler | `CAN_Handler.c/h` | Command processing + telemetry broadcast |
| Power Electronic | `Power_Electronic.c/h` | High-side switch control, ADC sensing |
| Fan PWM | `Fan_PWM.c/h` | 5-channel PWM output + tachometer DMA |
| Endstop | `Endstop.c/h` | 5 endstop switches (NC+NO / NO-only) |
| ESTOP | `ESTOP.c/h` | Emergency stop with debounce state machine |
| EEPROM Driver | `eeprom_driver.c/h` | I2C EEPROM startup config (8-byte struct) |
| FDCAN | `fdcan.c/h` | Low-level FDCAN peripheral + TX helper |
