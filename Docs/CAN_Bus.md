# Fabrica STM32G0 CAN Bus — Single-Page Reference

This document describes the shared CAN bus that the three Fabrica STM32G0B1 modules use to coexist:

- **KincoDrive Control Module V5.4** (`STM32G0B1_Applciationprog/KincoDrive_ControlModule_V5_4`)
- **PowerStage Controller** (`STM32G0B1_Applciationprog/PowerStage`)
- **LEDDriver** (`STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG`)

Each module has its own application + OpenBLT bootloader and is intended to share one 500 kbps classic-CAN bus with the others.

---

## 1. Bus parameters

| Setting | Value |
|---|---|
| Baud rate | 500 kbps (nominal) |
| Frame format | Classic CAN (no FD) |
| ID type | 11-bit standard only |
| Sample point target | 75 % |
| CANopen coexistence | Yes — IDs allocated in CiA 301 reserved gap `0x101-0x17F` |

---

## 2. Address map

```
0x000             ┐ CANopen NMT          (avoid)
0x001-0x07F       │ NMT reserved         (avoid)
0x080             │ CANopen SYNC         (avoid)
0x081-0x0FF       │ CANopen EMCY         (avoid)
0x100             │ CANopen TIME         (avoid)
0x101-0x12F       │ KincoDrive           ← FABRICA
0x130-0x15F       │ PowerStage           ← FABRICA
0x160-0x17F       │ LEDDriver            ← FABRICA
0x181-0x1FF       │ CANopen TPDO1        (avoid)
0x200-0x27F       │ CANopen RPDO1        (avoid)
… (CANopen ranges through 0x77F) …
0x780-0x7DF       │ Reserved (free)
0x7E4 / 0x7E5     │ CANopen LSS          (avoid)
```

**Per-device sub-block:**

```
0xN00 / 0xN01     Bootloader RX / TX  (OpenBLT XCP)
                  0xN00 also handles app's "enter bootloader" trigger
0xN10-0xN3F       Application command RX
0xN40-0xN7F       Application broadcast TX
```

---

## 3. Device address summary

| Device | BLT RX (also app reset) | BLT TX | App cmds | App bcasts |
|---|---|---|---|---|
| **KincoDrive** | `0x101` | `0x102` | `0x110-0x114` | `0x120-0x127` |
| **PowerStage** | `0x130` | `0x131` | `0x140-0x145` | `0x150-0x159` |
| **LEDDriver**  | `0x160` | `0x161` | `0x170-0x172` | `0x178-0x17A` |

**Per-device DBCs**

| Device | DBC file |
|---|---|
| KincoDrive | `STM32G0B1_Applciationprog/KincoDrive_ControlModule_V5_4/KincoDrive_ControlModule.dbc` |
| PowerStage | `STM32G0B1_Applciationprog/PowerStage/PowerStage.dbc` |
| LEDDriver  | `STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG/LEDDriver.dbc` |

**Combined bus DBC** (load this in PCAN-Explorer or Vector CANalyzer to monitor all three at once): `Docs/Fabrica_Bus.dbc`.

---

## 4. Detailed message catalogue

### KincoDrive — `0x101-0x127`

Low-level driver build. Protections: OC per HS channel + UV per bus only (no ESTOP, no Endstop, no PG-missing auto-shutdown).

| ID | Name | Dir | DLC | Cycle | Notes |
|---|---|---|---|---|---|
| `0x101` | Bootloader RX / Reset | RX | 2 | Event | byte[0]=0xFF triggers reset; bootloader then re-handles XCP CONNECT |
| `0x102` | Bootloader TX | TX | - | Event | OpenBLT XCP responses |
| `0x110` | CMD_HS_POWER | RX | 1 | Event | bit0=Drive bit1=Extruder bit2=Scrubbing bit3=12V_Buck. Setting an HS bit also clears any latched OC error for that channel |
| `0x111` | CMD_FAN_PWM | RX | 5 | Event | DR/EP/EH/ST/SF speeds 0-100 % |
| `0x112` | CMD_EEPROM | RX | 1 | Event | 0=load defaults+apply, 1=snapshot current to EEPROM |
| `0x113` | CMD_OC_THRESHOLD | RX | 6 | Event | uint16 LE × 3 (DR/EXT/SC) in mA |
| `0x114` | CMD_UV_THRESHOLD | RX | 4 | Event | uint16 LE × 2 (V24/V12) in mV |
| `0x120` | BCAST_STATUS | TX | 8 | 500 ms | V24/V12/I_24V (LE) + sys_state + error_mask |
| `0x121` | BCAST_CURRENTS | TX | 8 | 500 ms | bus + 3× HS currents (uint16 LE mA) |
| `0x122` | BCAST_TEMPS | TX | 6 | 500 ms | 6× PTC, value = degC + 40 |
| `0x123` | BCAST_FANS | TX | 5 | 500 ms | Fan tachometer % |
| `0x124` | BCAST_GPIO | TX | 8 | 500 ms | Full raw GPIO state (HS EN/PG/FT, VBUCK, B1, Toggle, LED2) |
| `0x125` | BCAST_RAW_ADC | TX | 6 | 500 ms | 12 channels, 4-bit nibbles |
| `0x126` | BCAST_CONFIG_A | TX | 8 | 500 ms | EEPROM hs_state + 3× OC thresholds |
| `0x127` | BCAST_CONFIG_B | TX | 8 | 500 ms | EEPROM UV thresholds + fan boot defaults |

Error mask bits (Bcast_Status byte 7): `0x01`=OC_DRIVE, `0x02`=OC_EXT, `0x04`=OC_SC, `0x08`=UV_24V, `0x10`=UV_12V. OC bits latch until host re-enables the channel via Cmd_HS_Power; UV bits auto-clear with 500 mV hysteresis.

### PowerStage — `0x130-0x159`

| ID | Name | Dir | DLC | Cycle | Notes |
|---|---|---|---|---|---|
| `0x130` | DEVICE_ADDR / Bootloader RX | RX | 2 | Event | byte[0]=0xFF triggers reset; bootloader then re-handles XCP CONNECT |
| `0x131` | Bootloader TX | TX | - | Event | OpenBLT XCP responses |
| `0x140` | CMD_FAN | RX | 2 | Event | mode + duty |
| `0x141` | CMD_HS | RX | 5 | Event | per-rail enable |
| `0x142` | CMD_OC | RX | 4 | Event | OC threshold / fault reset |
| `0x143` | CMD_EEPROM | RX | 1 | Event | save / load defaults |
| `0x144` | CMD_UV | RX | 6 | Event | UV thresholds |
| `0x145` | CMD_CTRL | RX | 2 | Event | V_LED_PWR + CAN relay |
| `0x146` | CMD_PAGE_DWELL | RX | 3 | Event | OLED per-page dwell (500 ms ticks per page) |
| `0x147` | CMD_BAT_CFG | RX | 1 | Event | SOC-low warning threshold (% SOC, 0 = disabled) |
| `0x150` | BCAST_HS_STATE | TX | 5 | 500 ms | Enable / fault / pgood / OC bitmasks |
| `0x151` | BCAST_HS_CURR_A | TX | 8 | 500 ms | BAT, CAP, SBC, DRIVE mA |
| `0x152` | BCAST_VOLTAGE | TX | 8 | 500 ms | V24, VCAP, V12 mV + UV fault mask |
| `0x153` | BCAST_FAN | TX | 4 | 500 ms | mode + duty + temp |
| `0x154` | BCAST_EEPROM | TX | 8 | 500 ms | Config echo |
| `0x155` | BCAST_HS_CURR_B | TX | 4 | 500 ms | AUX, LED mA |
| `0x156` | BCAST_UV | TX | 6 | 500 ms | Active UV thresholds |
| `0x157` | BCAST_OC_CFG_A | TX | 8 | 500 ms | OC thresholds AUX/LED/DRIVE/CAP |
| `0x158` | BCAST_OC_CFG_B | TX | 2 | 500 ms | OC threshold SBC |
| `0x159` | BCAST_IO_STATUS | TX | 3 | 500 ms | SW pin / V_LED_PWR / relay |
| `0x15A` | BCAST_BATTERY_CFG | TX | 8 | 500 ms | 6S battery static config: cutoff (19.6 V), full (25.2 V), R_int (200 mΩ), cell count |

PowerStage has dual CAN buses (FDCAN1 internal, FDCAN2 host gateway) and transparently relays any frame between them.

### LEDDriver — `0x160-0x17A`

| ID | Name | Dir | DLC | Cycle | Notes |
|---|---|---|---|---|---|
| `0x160` | DEVICEID / Bootloader RX | RX | 2 | Event | byte[0]=0xFF triggers reset; bootloader then re-handles XCP CONNECT |
| `0x161` | Bootloader TX | TX | - | Event | OpenBLT XCP responses |
| `0x170` | LIGHTSET | RX | 3 | Event | 3-channel PWM (0-100 %) |
| `0x171` | VOLTAGESET | RX | 5 | Event | UV thresholds (V24, V17.5) + buck control mode (byte 4: 0=OFF, 1=ON, 2=AUTO). DLC=4 form keeps the previous mode untouched. AUTO ties buck-enable to V24 ≥ UV_24V threshold |
| `0x172` | EEPROMSET | RX | 1 | Event | bit0 save / bit1 load defaults |
| `0x178` | EEPROMDATA | TX | 8 | ~300 ms | EEPROM config echo (UV thresholds, PWM defaults, buck mode). Phase 2 of the LIGHTSTATUS/DEVSTATUS/EEPROMDATA rotation |
| `0x179` | LIGHTSTATUS | TX | 8 | ~300 ms | PWM levels + voltages + heartbeat counter. Phase 0 of the rotation |
| `0x17A` | DEVSTATUS | TX | 2 | ~300 ms | system state + error code. Phase 1 of the rotation |

---

## 5. Bootloader workflow (per device)

The same XCP CONNECT frame works whether the application or the bootloader is currently running on the target. Example for PowerStage (device address `0x130`):

1. Host sends `ID=0x130, byte[0]=0xFF, dlc=2`.
2. If the **application** is running, its CAN ISR detects this frame, calls `NVIC_SystemReset()`. The MCU resets into the OpenBLT bootloader.
3. If the **bootloader** is running (either after that reset, or after a power-on if no valid app is flashed), it receives the same frame on the same ID, recognises it as XCP CONNECT, and responds on `0x131`.
4. Host proceeds with the normal OpenBLT XCP download sequence on the `0x130 / 0x131` pair.

The address pairs `(0x101/0x102)`, `(0x130/0x131)`, `(0x160/0x161)` are unique per device — three boards in bootloader at once will never collide with each other.

---

## 6. Verification checklist

- **Build all 6 projects** (3 bootloaders, 3 apps) in STM32CubeIDE — confirm no unresolved CAN ID symbols.
- **Static grep** the source tree for the old IDs (`0x0667`, `0x1F100`, `0x66[2-9A-F]`, `0x67[012]`, `0x7E1`, `0x030`, `0x010`, `0x040`) — only doc strings should remain.
- **CANopen overlap grep** — verify no source defines `0x080`, `0x100`, or anything in `0x180-0x77F` ranges as a CAN message ID.
- **Single-device flashing** — for each device, send `0xFF 0x00` (DLC=2) on `0xN00`, confirm the bootloader's TX appears on `0xN01`, then verify OpenBLT BootCommander download succeeds.
- **All three on one bus** — power all boards together, load `Docs/Fabrica_Bus.dbc` in your tool, confirm every periodic broadcast decodes and there are no ID collisions.
- **PowerStage relay** — with PowerStage in the middle, send a command on its CAN2 (host bus). Confirm the same frame appears on CAN1 and is processed by the local app.

---

## 7. Migration note

The earlier address scheme (KincoDrive ext `0xMSG0667`, PowerStage std `0x66x`, LEDDriver ext `0x1F100xx`, all bootloaders TX `0x7E1`) is no longer in use. Any host-side test scripts, PCAN-Explorer projects (`KincoDrivePcanTest.peproj`, `PowerStageTest.peproj`), or external integration code with hard-coded IDs must be regenerated against the new DBCs.
