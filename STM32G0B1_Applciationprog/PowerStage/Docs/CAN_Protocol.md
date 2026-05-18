# PowerStage — CAN Bus Protocol Reference

**Device:** STM32G0B1 PowerStage Controller
**MCU Peripherals:** FDCAN1 (internal bus) + FDCAN2 (host/relay bus)
**Nominal Baud Rate:** 500 kbps (both buses)
**Frame Format:** 11-bit Standard ID, DLC <= 8, Classic CAN
**DBC File:** `PowerStage.dbc`

---

## 1. Network Nodes

| Node | Role |
|---|---|
| `PowerStage` | Embedded controller (this device). Manages hot-swap rails, fan, ADC, EEPROM. |
| `Master` | Supervisory host or gateway. Sends commands and receives status broadcasts. |

---

## 2. Dual-Bus Architecture

The PowerStage uses two CAN peripherals as a transparent relay / gateway:

```
                    +-----------+
   Host / PC  <---->| FDCAN2    |    CAN2 (host bus)
   test tool        | PC2=RX    |    PB: 500 kbps
                    | PC3=TX    |
                    +-----------+
                         |  relay (bidirectional, no filter)
                    +-----------+
   Other nodes <--->| FDCAN1    |    CAN1 (internal bus)
   on internal bus  | PB8=RX    |    PB: 500 kbps
                    | PB9=TX    |    (OpenBLT bootloader primary)
                    +-----------+
```

| Bus | Peripheral | GPIO | Role |
|---|---|---|---|
| CAN1 (internal) | FDCAN1 / `canHandle` | PB8 (RX), PB9 (TX) | Primary bus — bootloader + application. Commands processed + broadcasts sent here. |
| CAN2 (host) | FDCAN2 / `hfdcan2` | PC2 (RX), PC3 (TX) | Host interface — relay gateway. All frames mirrored to/from CAN1. |

**Relay behaviour:**

- **CAN2 RX -> CAN1 TX:** Any frame received on the host bus is forwarded raw to the internal bus AND parsed for commands.
- **CAN1 RX -> CAN2 TX:** Any frame received on the internal bus is forwarded raw to the host bus AND parsed for commands.
- **Broadcasts:** All periodic broadcasts are sent on BOTH CAN1 and CAN2 simultaneously via `CAN_SendAll()`.
- **No filter:** All standard CAN IDs are accepted on both buses. No hardware message filtering is applied.

---

## 3. Encoding Conventions

| Type | Encoding | DBC notation | Scale to engineering unit |
|---|---|---|---|
| `uint8_t` | Intel byte order | `@1+` | x 1 |
| `uint16_t` voltage/current | Big-endian (Motorola) | `@0+` | x 0.001 -> V or A |
| `uint16_t` threshold | Big-endian (Motorola) | `@0+` | x 0.001 -> V or A |
| `int16_t` temperature | Big-endian signed (Motorola) | `@0-` | x 0.1 -> C |
| bitmask | Intel byte order | `@1+` | x 1, bit-decoded |

---

## 4. CAN ID Map

```
Direction  | CAN ID | Name              | DLC | Sender      | Cycle
-----------+--------+-------------------+-----+-------------+-------
TX (bcast) | 0x662  | BCAST_HS_STATE    |  5  | PowerStage  | 500 ms
TX (bcast) | 0x663  | BCAST_HS_CURR_A   |  8  | PowerStage  | 500 ms
TX (bcast) | 0x664  | BCAST_VOLTAGE     |  8  | PowerStage  | 500 ms
RX (cmd)   | 0x665  | CMD_FAN           |  2  | Master      | Event
RX (cmd)   | 0x666  | CMD_HS            |  5  | Master      | Event
RX (cmd)   | 0x667  | DEVICE_ADDR       |  2  | Master      | Event
RX (cmd)   | 0x668  | CMD_OC            |  4  | Master      | Event
RX (cmd)   | 0x669  | CMD_EEPROM        |  1  | Master      | Event
TX (bcast) | 0x66A  | BCAST_FAN         |  4  | PowerStage  | 500 ms
TX (bcast) | 0x66B  | BCAST_EEPROM      |  8  | PowerStage  | 500 ms
TX (bcast) | 0x66C  | BCAST_HS_CURR_B   |  4  | PowerStage  | 500 ms
RX (cmd)   | 0x66D  | CMD_UV            |  6  | Master      | Event
TX (bcast) | 0x66E  | BCAST_UV          |  6  | PowerStage  | 500 ms
TX (bcast) | 0x66F  | BCAST_OC_CFG_A    |  8  | PowerStage  | 500 ms
TX (bcast) | 0x670  | BCAST_OC_CFG_B    |  2  | PowerStage  | 500 ms
RX (cmd)   | 0x671  | CMD_CTRL          |  2  | Master      | Event
TX (bcast) | 0x672  | BCAST_IO_STATUS   |  3  | PowerStage  | 500 ms
```

**Note:** All TX broadcasts and RX commands are mirrored on both CAN1 and CAN2 buses (when relay is enabled).

---

## 5. Message Descriptions

---

### 0x662 — BCAST_HS_STATE (TX, DLC = 5)

Periodic broadcast of all five hot-swap rail status bitmasks.
Bit mapping for all masks: **bit 0 = RAIL_AUX, bit 1 = RAIL_LED, bit 2 = RAIL_DRIVE, bit 3 = RAIL_CAP, bit 4 = RAIL_SBC**

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `HS_Enable_Mask` | uint8 bitmask | 1 = EN GPIO asserted (rail enabled) |
| 1 | `HS_Fault_Mask` | uint8 bitmask | 1 = TPS2493 hardware FLT pin asserted |
| 2 | `HS_PGood_Mask` | uint8 bitmask | 1 = TPS2493 PGOOD pin asserted |
| 3 | `HS_OC_Warn_Mask` | uint8 bitmask | 1 = software OC warning threshold exceeded |
| 4 | `HS_OC_Fault_Mask` | uint8 bitmask | 1 = hardware OC fault latched |

**Example — all rails enabled, no faults:**
```
Byte: 0x1F  0x00  0x1F  0x00  0x00
       ^EN   ^FLT  ^PG   ^OCw  ^OCf
```

---

### 0x663 — BCAST_HS_CURR_A (TX, DLC = 8)

Current measurements for BAT, CAP, SBC, DRIVE rails.
All values are `uint16_t mA` big-endian. **Scale x 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0-1 | `I_BAT_mA` | 0-50000 | mA |
| 2-3 | `I_CAP_mA` | 0-50000 | mA |
| 4-5 | `I_SBC_mA` | 0-50000 | mA |
| 6-7 | `I_DRIVE_mA` | 0-50000 | mA |

---

### 0x664 — BCAST_VOLTAGE (TX, DLC = 8)

Bus voltage measurements and UV fault status.
Voltage values are `uint16_t mV` big-endian. **Scale x 0.001 to get Volts.**

| Bytes | Signal | Range | Unit | Description |
|---|---|---|---|---|
| 0-1 | `V24_mV` | 0-30000 | mV | 24V bus |
| 2-3 | `VCAP_mV` | 0-30000 | mV | Capacitor bus |
| 4-5 | `V12_mV` | 0-15000 | mV | 12V bus |
| 6 | `UV_Fault_Mask` | 0-7 | — | bit 0 = V24 UV, bit 1 = VCAP UV, bit 2 = V12 UV |
| 7 | *(reserved)* | — | — | — |

---

### 0x665 — CMD_FAN (RX, DLC = 2)

Set fan operating mode and manual duty cycle.

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `Fan_Mode` | 0 = OFF, 1 = ON_MANUAL, 2 = AUTO | Operating mode |
| 1 | `Fan_Duty` | 0-100 | Duty cycle %. Only applied when Mode = 1 |

**AUTO mode behaviour:** Fan turns ON at full speed when temperature >= `Cfg_Fan_Auto_On_Temp` and turns OFF when temperature < `Cfg_Fan_Auto_Off_Temp` (hysteresis).

---

### 0x666 — CMD_HS (RX, DLC = 5)

Enable or disable individual hot-swap rails.

| Byte | Signal | Values | Rail |
|---|---|---|---|
| 0 | `HS_Cmd_AUX` | 0 = DISABLE, 1 = ENABLE | AUX |
| 1 | `HS_Cmd_LED` | 0 = DISABLE, 1 = ENABLE | LED |
| 2 | `HS_Cmd_DRIVE` | 0 = DISABLE, 1 = ENABLE | DRIVE |
| 3 | `HS_Cmd_CAP` | 0 = DISABLE, 1 = ENABLE | CAP |
| 4 | `HS_Cmd_SBC` | 0 = DISABLE, 1 = ENABLE | SBC *(no EN GPIO — reserved)* |

---

### 0x667 — DEVICE_ADDR (RX, DLC = 2)

System-level device commands.

| Byte | Signal | Value | Action |
|---|---|---|---|
| 0 | `Device_Command` | `0xFF` | Software reset -> re-enter OpenBLT bootloader |

---

### 0x668 — CMD_OC (RX, DLC = 4)

Configure per-rail software overcurrent thresholds or reset latched faults.

| Byte | Signal | Description |
|---|---|---|
| 0 | `OC_Rail_Mask` | Target rail bitmask (bit0=AUX...bit4=SBC). `0xFF` = all rails |
| 1 | `OC_Cmd` | `0x01` = SET_THRESHOLD, `0x02` = RESET_FAULT |
| 2-3 | `OC_Threshold_mA` | New threshold in mA, big-endian uint16. Applied when `OC_Cmd = 0x01` |

**SET_THRESHOLD example — set AUX + LED to 2 A (2000 mA = 0x07D0):**
```
Byte: 0x03  0x01  0x07  0xD0
       ^AUX+LED  ^SET  ^2000mA high  ^2000mA low
```

**RESET_FAULT example — clear all OC flags:**
```
Byte: 0xFF  0x02  0x00  0x00
       ^ALL  ^RST  ---- (ignored)
```

---

### 0x669 — CMD_EEPROM (RX, DLC = 1)

EEPROM config operations.

| Byte | Signal | Value | Action |
|---|---|---|---|
| 0 | `EEPROM_Cmd` | `0x01` = SAVE_CONFIG | Snapshot live config -> EEPROM |
| 0 | `EEPROM_Cmd` | `0x02` = LOAD_DEFAULT | Restore factory defaults, overwrite EEPROM |

---

### 0x66A — BCAST_FAN (TX, DLC = 4)

Periodic broadcast of fan state and NTC temperature.

| Bytes | Signal | Type | Scale | Unit | Description |
|---|---|---|---|---|---|
| 0 | `Fan_Mode_State` | uint8 | x 1 | — | 0=OFF 1=ON_MANUAL 2=AUTO |
| 1 | `Fan_Duty_State` | uint8 | x 1 | % | Current duty cycle 0-100 |
| 2-3 | `Temperature_C` | int16 big-endian | x 0.1 | C | NTC temperature x 10. `0x00FD` = 25.3 C |

---

### 0x66B — BCAST_EEPROM (TX, DLC = 8)

Periodic echo of the currently stored EEPROM configuration.

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `Cfg_Fan_Default_Mode` | uint8 | Boot fan mode |
| 1 | `Cfg_Fan_Default_Duty` | uint8 | Boot fan duty % |
| 2 | `Cfg_Fan_Min_Duty` | uint8 | Minimum PWM duty % |
| 3 | `Cfg_Fan_Auto_On_Temp` | uint8 | AUTO ON temperature (C) |
| 4 | `Cfg_Fan_Auto_Off_Temp` | uint8 | AUTO OFF temperature (C, hysteresis) |
| 5 | `Cfg_HS_Default_State` | uint8 | HS enable bitmask applied at boot |
| 6-7 | *(reserved)* | — | — |

---

### 0x66C — BCAST_HS_CURR_B (TX, DLC = 4)

Current measurements for AUX and LED rails.
Values are `uint16_t mA` big-endian. **Scale x 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0-1 | `I_AUX_mA` | 0-50000 | mA |
| 2-3 | `I_LED_mA` | 0-50000 | mA |

---

### 0x66D — CMD_UV (RX, DLC = 6)

Set undervoltage trip thresholds for all three voltage rails.
Values are `uint16_t mV` big-endian.

| Bytes | Signal | Default | Description |
|---|---|---|---|
| 0-1 | `UV_Threshold_V24_mV` | 20000 | V24 UV trip point (mV) |
| 2-3 | `UV_Threshold_VCAP_mV` | 20000 | VCAP UV trip point (mV) |
| 4-5 | `UV_Threshold_V12_mV` | 10000 | V12 UV trip point (mV) |

---

### 0x66E — BCAST_UV (TX, DLC = 6)

Periodic echo of the currently active UV thresholds.

| Bytes | Signal | Unit |
|---|---|---|
| 0-1 | `UV_Active_V24_mV` | mV |
| 2-3 | `UV_Active_VCAP_mV` | mV |
| 4-5 | `UV_Active_V12_mV` | mV |

---

### 0x66F — BCAST_OC_CFG_A (TX, DLC = 8)

Periodic echo of per-rail OC thresholds (rails AUX, LED, DRIVE, CAP).
Values are `uint16_t mA` big-endian. **Scale x 0.001 to get Amperes.**

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0-1 | `OC_Threshold_AUX_mA` | 5000 |
| 2-3 | `OC_Threshold_LED_mA` | 2000 |
| 4-5 | `OC_Threshold_DRIVE_mA` | 5000 |
| 6-7 | `OC_Threshold_CAP_mA` | 5000 |

---

### 0x670 — BCAST_OC_CFG_B (TX, DLC = 2)

Periodic echo of OC threshold for SBC rail (continuation of frame A).

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0-1 | `OC_Threshold_SBC_mA` | 5000 |

---

### 0x671 — CMD_CTRL (RX, DLC = 2)

Control V_LED_PWR output and CAN relay enable/disable.

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `V_LED_PWR_Cmd` | 0 = OFF, 1 = ON | Controls V_LED_PWR output (PC0) |
| 1 | `CAN_Relay_Enable` | 0 = DISABLE, 1 = ENABLE | Controls CAN1<->CAN2 frame forwarding |

**Note:** Both fields are applied on every frame. The master must specify both values.

**Turn on V_LED_PWR, relay enabled:**
```
ID: 0x671  DLC: 2  Data: 01 01
```

**Turn off V_LED_PWR, disable relay:**
```
ID: 0x671  DLC: 2  Data: 00 00
```

---

### 0x672 — BCAST_IO_STATUS (TX, DLC = 3)

Periodic broadcast of GPIO and relay status.

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `SW_Pin_State` | uint8 | Current state of SW pin (PC1). 0=LOW, 1=HIGH |
| 1 | `V_LED_PWR_State` | uint8 | Current V_LED_PWR output state. 0=OFF, 1=ON |
| 2 | `CAN_Relay_State` | uint8 | CAN relay enabled. 0=disabled, 1=enabled |

---

## 6. Fault & Error Code Bitmask

The following bitmask is used internally (`display_data.error_code`) and can be inferred from the broadcast messages:

| Bit | Define | Source signal |
|---|---|---|
| 0 | `ERR_UV_V24` | `BCAST_VOLTAGE.UV_Fault_Mask` bit 0 |
| 1 | `ERR_UV_VCAP` | `BCAST_VOLTAGE.UV_Fault_Mask` bit 1 |
| 2 | `ERR_UV_V12` | `BCAST_VOLTAGE.UV_Fault_Mask` bit 2 |
| 3 | `ERR_OC_AUX` | `BCAST_HS_STATE.HS_OC_Warn_Mask` bit 0 |
| 4 | `ERR_OC_LED` | `BCAST_HS_STATE.HS_OC_Warn_Mask` bit 1 |
| 5 | `ERR_OC_DRIVE` | `BCAST_HS_STATE.HS_OC_Warn_Mask` bit 2 |
| 6 | `ERR_OC_CAP` | `BCAST_HS_STATE.HS_OC_Warn_Mask` bit 3 |
| 7 | `ERR_OC_SBC` | `BCAST_HS_STATE.HS_OC_Warn_Mask` bit 4 |
| 8 | `ERR_HS_FLT_AUX` | `BCAST_HS_STATE.HS_Fault_Mask` bit 0 |
| 9 | `ERR_HS_FLT_LED` | `BCAST_HS_STATE.HS_Fault_Mask` bit 1 |
| 10 | `ERR_HS_FLT_DRV` | `BCAST_HS_STATE.HS_Fault_Mask` bit 2 |
| 11 | `ERR_HS_FLT_CAP` | `BCAST_HS_STATE.HS_Fault_Mask` bit 3 |
| 12 | `ERR_HS_FLT_SBC` | `BCAST_HS_STATE.HS_Fault_Mask` bit 4 |
| 13 | `ERR_OVERHEAT` | `BCAST_FAN.Temperature_C` >= 800 (80.0 C) |

---

## 7. Application State Machine

```
                  +----------------+
   Power-On ----> |  APP_STATE_    |
                  |    INIT        | one-shot: probe I2C, load EEPROM,
                  |                | init HS + fan + ADC + CAN2, show splash
                  +-------+--------+
                          | init complete
                          v
              +------------------------+
     +------->|   APP_STATE_RUNNING    |<------------------+
     |        |  measure / broadcast   |                   |
     |        |  fan auto control      |                   |
     |        |  handle CAN commands   |                   |
     |        |  CAN relay (CAN1<>CAN2)|                   |
     |        +-------+----------------+                   |
     |                | error_code != 0                    |
     |                v                                    |
     |        +------------------------+                   |
     |        |   APP_STATE_FAULT      | error_code == 0 --+
     +----- --|  same tasks, but       |
              |  UI shows SYS_FAULT    |
              +------------------------+
```

**State transitions:**

| From | To | Condition |
|---|---|---|
| `INIT` | `RUNNING` | Init sequence complete (one-shot on first `PS_App_Task()` call) |
| `RUNNING` | `FAULT` | Any `ERR_xxx` bit set in `display_data.error_code` |
| `FAULT` | `RUNNING` | All `ERR_xxx` bits cleared |

---

## 8. Typical Boot Sequence

```
1.  MCU reset -- VectorBase_Config() remaps interrupt table
2.  HAL_Init() + SystemClock_Config() + MX peripherals
3.  AppInit()     -- OpenBLT bootloader hook (starts FDCAN1 as canHandle)
4.  PS_App_Init() -- CANInitTxHeader() (CAN1 TX header + RX notification)
5.  [Loop] AppTask() + PS_App_Task()
       +-> First call enters APP_STATE_INIT:
           a. Probe EEPROM (0xA0) via I2C
           b. Load Config or LoadDefault()
           c. HS_init() -- disable all -> 100ms -> enable AUX/LED/DRIVE
           d. fan_init() -- start TIM1 PWM
           e. Apply_Config() -- push EEPROM values to oc_status/uv_status/fan
           f. CAN2_Host_Init() -- start FDCAN2 host bus + RX + no-filter
           g. HAL_ADC_Start_DMA() -- continuous ADC scan to adc_buffer[]
           h. Probe OLED (0x78) -- SSD1306_Init() + splash "POWERSTAGE INIT"
           i. DisplayScheduler_Init()
           j. Transition -> APP_STATE_RUNNING
```

---

## 9. EEPROM Config Layout (I2C address 0xA0)

Stored at page 1, offset 0. Total size: **24 bytes**, packed.

| Offset | Size | Field | Default |
|---|---|---|---|
| 0 | 2 | `magic` (0xAB12) | 0xAB12 |
| 2 | 1 | `fan_default_mode` | 0 (OFF) |
| 3 | 1 | `fan_default_duty` | 50 % |
| 4 | 1 | `fan_min_duty` | 20 % |
| 5 | 1 | `fan_auto_on_temp` | 50 C |
| 6 | 1 | `fan_auto_off_temp` | 45 C |
| 7 | 1 | `hs_default_state` | 0x1F (all enabled) |
| 8-17 | 10 | `oc_threshold_mA[5]` | {5000, 2000, 5000, 5000, 5000} mA |
| 18-19 | 2 | `uv_V24_mV` | 20000 mV |
| 20-21 | 2 | `uv_VCAP_mV` | 20000 mV |
| 22-23 | 2 | `uv_V12_mV` | 10000 mV |

A `magic` mismatch triggers `LoadDefault()` and overwrites the EEPROM.

---

## 10. Hardware Rail Index

| Index | Name | TPS2493 | Sense R | EN GPIO | Notes |
|---|---|---|---|---|---|
| 0 | RAIL_AUX | Yes | 3 mR | EN_AUX (PA10) | -- |
| 1 | RAIL_LED | Yes | 3 mR | EN_LED (PD1) | LED supply |
| 2 | RAIL_DRIVE | Yes | 3 mR | EN_DRIVE (PD4) | Motor drive |
| 3 | RAIL_CAP | Yes | 3 mR | EN_CAP (PB3) | Delayed at boot |
| 4 | RAIL_SBC | Yes | 3 mR | *(none)* | SBC -- no EN GPIO |

---

## 11. Quick-Reference: CAN Commands for Testing

Send these frames from your CAN tool (on either CAN1 or CAN2) to control the PowerStage:

**Enable all HS rails:**
```
ID: 0x666  DLC: 5  Data: 01 01 01 01 01
```

**Disable all HS rails:**
```
ID: 0x666  DLC: 5  Data: 00 00 00 00 00
```

**Fan ON manual 75%:**
```
ID: 0x665  DLC: 2  Data: 01 4B
```

**Fan AUTO mode:**
```
ID: 0x665  DLC: 2  Data: 02 00
```

**Fan OFF:**
```
ID: 0x665  DLC: 2  Data: 00 00
```

**Set all OC thresholds to 3A (3000 mA = 0x0BB8):**
```
ID: 0x668  DLC: 4  Data: FF 01 0B B8
```

**Reset all OC faults:**
```
ID: 0x668  DLC: 4  Data: FF 02 00 00
```

**Set UV thresholds (V24=18V, VCAP=18V, V12=9V):**
```
ID: 0x66D  DLC: 6  Data: 46 50 46 50 23 28
                          ^18000mV ^18000mV ^9000mV
```

**Save config to EEPROM:**
```
ID: 0x669  DLC: 1  Data: 01
```

**Load factory defaults:**
```
ID: 0x669  DLC: 1  Data: 02
```

**Reset to bootloader:**
```
ID: 0x667  DLC: 2  Data: FF 00
```

**Turn on V_LED_PWR, relay enabled:**
```
ID: 0x671  DLC: 2  Data: 01 01
```

**Turn off V_LED_PWR, disable CAN relay:**
```
ID: 0x671  DLC: 2  Data: 00 00
```
