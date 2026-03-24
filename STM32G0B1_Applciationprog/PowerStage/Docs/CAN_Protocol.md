# PowerStage — CAN Bus Protocol Reference

**Device:** STM32G0B1 PowerStage Controller
**MCU Peripheral:** FDCAN2 (Classic CAN frame mode)
**Nominal Baud Rate:** 500 kbps
**Frame Format:** 11-bit Standard ID, DLC ≤ 8
**DBC File:** `PowerStage.dbc`

---

## 1. Network Nodes

| Node | Role |
|---|---|
| `PowerStage` | Embedded controller (this device). Manages hot-swap rails, fan, ADC, EEPROM. |
| `Master` | Supervisory host or gateway. Sends commands and receives status broadcasts. |

---

## 2. Encoding Conventions

| Type | Encoding | DBC notation | Scale to engineering unit |
|---|---|---|---|
| `uint8_t` | Intel byte order | `@1+` | × 1 |
| `uint16_t` voltage/current | Big-endian (Motorola) | `@0+` | × 0.001 → V or A |
| `uint16_t` threshold | Big-endian (Motorola) | `@0+` | × 0.001 → V or A |
| `int16_t` temperature | Big-endian signed (Motorola) | `@0-` | × 0.1 → °C |
| bitmask | Intel byte order | `@1+` | × 1, bit-decoded |

---

## 3. CAN ID Map

```
Direction  │ CAN ID │ Name              │ DLC │ Sender
───────────┼────────┼───────────────────┼─────┼────────────
TX (bcast) │ 0x662  │ BCAST_HS_STATE    │  5  │ PowerStage
TX (bcast) │ 0x663  │ BCAST_HS_CURR_A   │  8  │ PowerStage
TX (bcast) │ 0x664  │ BCAST_VOLTAGE     │  8  │ PowerStage
RX (cmd)   │ 0x665  │ CMD_FAN           │  2  │ Master
RX (cmd)   │ 0x666  │ CMD_HS            │  5  │ Master
RX (cmd)   │ 0x667  │ DEVICE_ADDR       │  2  │ Master
RX (cmd)   │ 0x668  │ CMD_OC            │  4  │ Master
RX (cmd)   │ 0x669  │ CMD_EEPROM        │  1  │ Master
TX (bcast) │ 0x66A  │ BCAST_FAN         │  4  │ PowerStage
TX (bcast) │ 0x66B  │ BCAST_EEPROM      │  8  │ PowerStage
TX (bcast) │ 0x66C  │ BCAST_HS_CURR_B   │  4  │ PowerStage
RX (cmd)   │ 0x66D  │ CMD_UV            │  6  │ Master
TX (bcast) │ 0x66E  │ BCAST_UV          │  6  │ PowerStage
TX (bcast) │ 0x66F  │ BCAST_OC_CFG_A    │  8  │ PowerStage
TX (bcast) │ 0x670  │ BCAST_OC_CFG_B    │  2  │ PowerStage
```

---

## 4. Message Descriptions

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
       ↑EN   ↑FLT  ↑PG   ↑OCw  ↑OCf
```

---

### 0x663 — BCAST_HS_CURR_A (TX, DLC = 8)

Current measurements for BAT, CAP, SBC, DRIVE rails.
All values are `uint16_t mA` big-endian. **Scale × 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0–1 | `I_BAT_mA` | 0–50 000 | mA |
| 2–3 | `I_CAP_mA` | 0–50 000 | mA |
| 4–5 | `I_SBC_mA` | 0–50 000 | mA |
| 6–7 | `I_DRIVE_mA` | 0–50 000 | mA |

---

### 0x664 — BCAST_VOLTAGE (TX, DLC = 8)

Bus voltage measurements and UV fault status.
Voltage values are `uint16_t mV` big-endian. **Scale × 0.001 to get Volts.**

| Bytes | Signal | Range | Unit | Description |
|---|---|---|---|---|
| 0–1 | `V24_mV` | 0–30 000 | mV | 24V bus |
| 2–3 | `VCAP_mV` | 0–30 000 | mV | Capacitor bus |
| 4–5 | `V12_mV` | 0–15 000 | mV | 12V bus |
| 6 | `UV_Fault_Mask` | 0–7 | — | bit 0 = V24 UV, bit 1 = VCAP UV, bit 2 = V12 UV |
| 7 | *(reserved)* | — | — | — |

---

### 0x665 — CMD_FAN (RX, DLC = 2)

Set fan operating mode and manual duty cycle.

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `Fan_Mode` | 0 = OFF, 1 = ON_MANUAL, 2 = AUTO | Operating mode |
| 1 | `Fan_Duty` | 0–100 | Duty cycle %. Only applied when Mode = 1 |

**AUTO mode behaviour:** Fan turns ON at full speed when temperature ≥ `Cfg_Fan_Auto_On_Temp` and turns OFF when temperature < `Cfg_Fan_Auto_Off_Temp` (hysteresis).

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
| 0 | `Device_Command` | `0xFF` | Software reset → re-enter OpenBLT bootloader |

---

### 0x668 — CMD_OC (RX, DLC = 4)

Configure per-rail software overcurrent thresholds or reset latched faults.

| Byte | Signal | Description |
|---|---|---|
| 0 | `OC_Rail_Mask` | Target rail bitmask (bit0=AUX…bit4=SBC). `0xFF` = all rails |
| 1 | `OC_Cmd` | `0x01` = SET_THRESHOLD · `0x02` = RESET_FAULT |
| 2–3 | `OC_Threshold_mA` | New threshold in mA, big-endian uint16. Applied when `OC_Cmd = 0x01` |

**SET_THRESHOLD example — set AUX + LED to 2 A (2000 mA = 0x07D0):**
```
Byte: 0x03  0x01  0x07  0xD0
       ↑AUX+LED  ↑SET  ↑2000mA high  ↑2000mA low
```

**RESET_FAULT example — clear all OC flags:**
```
Byte: 0xFF  0x02  0x00  0x00
       ↑ALL  ↑RST  ──── (ignored)
```

---

### 0x669 — CMD_EEPROM (RX, DLC = 1)

EEPROM config operations.

| Byte | Signal | Value | Action |
|---|---|---|---|
| 0 | `EEPROM_Cmd` | `0x01` = SAVE_CONFIG | Snapshot live config → EEPROM |
| 0 | `EEPROM_Cmd` | `0x02` = LOAD_DEFAULT | Restore factory defaults, overwrite EEPROM |

---

### 0x66A — BCAST_FAN (TX, DLC = 4)

Periodic broadcast of fan state and NTC temperature.

| Bytes | Signal | Type | Scale | Unit | Description |
|---|---|---|---|---|---|
| 0 | `Fan_Mode_State` | uint8 | × 1 | — | 0=OFF 1=ON_MANUAL 2=AUTO |
| 1 | `Fan_Duty_State` | uint8 | × 1 | % | Current duty cycle 0–100 |
| 2–3 | `Temperature_C` | int16 big-endian | × 0.1 | °C | NTC temperature × 10. `0x00FD` = 25.3 °C |

---

### 0x66B — BCAST_EEPROM (TX, DLC = 8)

Periodic echo of the currently stored EEPROM configuration.

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `Cfg_Fan_Default_Mode` | uint8 | Boot fan mode |
| 1 | `Cfg_Fan_Default_Duty` | uint8 | Boot fan duty % |
| 2 | `Cfg_Fan_Min_Duty` | uint8 | Minimum PWM duty % |
| 3 | `Cfg_Fan_Auto_On_Temp` | uint8 | AUTO ON temperature (°C) |
| 4 | `Cfg_Fan_Auto_Off_Temp` | uint8 | AUTO OFF temperature (°C, hysteresis) |
| 5 | `Cfg_HS_Default_State` | uint8 | HS enable bitmask applied at boot |
| 6–7 | *(reserved)* | — | — |

---

### 0x66C — BCAST_HS_CURR_B (TX, DLC = 4)

Current measurements for AUX and LED rails.
Values are `uint16_t mA` big-endian. **Scale × 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0–1 | `I_AUX_mA` | 0–50 000 | mA |
| 2–3 | `I_LED_mA` | 0–50 000 | mA |

---

### 0x66D — CMD_UV (RX, DLC = 6)

Set undervoltage trip thresholds for all three voltage rails.
Values are `uint16_t mV` big-endian.

| Bytes | Signal | Default | Description |
|---|---|---|---|
| 0–1 | `UV_Threshold_V24_mV` | 20 000 | V24 UV trip point (mV) |
| 2–3 | `UV_Threshold_VCAP_mV` | 20 000 | VCAP UV trip point (mV) |
| 4–5 | `UV_Threshold_V12_mV` | 10 000 | V12 UV trip point (mV) |

---

### 0x66E — BCAST_UV (TX, DLC = 6)

Periodic echo of the currently active UV thresholds.

| Bytes | Signal | Unit |
|---|---|---|
| 0–1 | `UV_Active_V24_mV` | mV |
| 2–3 | `UV_Active_VCAP_mV` | mV |
| 4–5 | `UV_Active_V12_mV` | mV |

---

### 0x66F — BCAST_OC_CFG_A (TX, DLC = 8)

Periodic echo of per-rail OC thresholds (rails AUX, LED, DRIVE, CAP).
Values are `uint16_t mA` big-endian. **Scale × 0.001 to get Amperes.**

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0–1 | `OC_Threshold_AUX_mA` | 5 000 |
| 2–3 | `OC_Threshold_LED_mA` | 2 000 |
| 4–5 | `OC_Threshold_DRIVE_mA` | 5 000 |
| 6–7 | `OC_Threshold_CAP_mA` | 5 000 |

---

### 0x670 — BCAST_OC_CFG_B (TX, DLC = 2)

Periodic echo of OC threshold for SBC rail (continuation of frame A).

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0–1 | `OC_Threshold_SBC_mA` | 5 000 |

---

## 5. Fault & Error Code Bitmask

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
| 13 | `ERR_OVERHEAT` | `BCAST_FAN.Temperature_C` ≥ 800 (80.0 °C) |

---

## 6. Application State Machine

```
                  ┌──────────────┐
   Power-On ────► │  APP_STATE_  │
                  │    INIT      │ one-shot: probe I2C, load EEPROM,
                  │              │ init HS + fan + ADC, show splash
                  └──────┬───────┘
                         │ init complete
                         ▼
              ┌──────────────────────┐
     ┌───────►│   APP_STATE_RUNNING  │◄────────────────┐
     │        │  measure / broadcast │                  │
     │        │  fan auto control    │                  │
     │        │  handle CAN commands │                  │
     │        └──────────┬───────────┘                  │
     │                   │ error_code != 0               │
     │                   ▼                               │
     │        ┌──────────────────────┐                   │
     │        │   APP_STATE_FAULT    │ error_code == 0 ──┘
     └────────│  same tasks, but     │
              │  UI shows SYS_FAULT  │
              └──────────────────────┘
```

**State transitions:**

| From | To | Condition |
|---|---|---|
| `INIT` | `RUNNING` | Init sequence complete (one-shot on first `PS_App_Task()` call) |
| `RUNNING` | `FAULT` | Any `ERR_xxx` bit set in `display_data.error_code` |
| `FAULT` | `RUNNING` | All `ERR_xxx` bits cleared |

---

## 7. Typical Boot Sequence

```
1.  MCU reset — VectorBase_Config() remaps interrupt table
2.  HAL_Init() + SystemClock_Config() + MX peripherals
3.  AppInit()     — OpenBLT bootloader hook
4.  PS_App_Init() — CANInitTxHeader()
5.  [Loop] AppTask() + PS_App_Task()
       └─► First call enters APP_STATE_INIT:
           a. Probe EEPROM (0xA0) via I2C
           b. Load Config or LoadDefault()
           c. HS_init() — disable all → 100ms → enable AUX/LED/DRIVE
           d. fan_init() — start TIM1 PWM
           e. Apply_Config() — push EEPROM values to oc_status/uv_status/fan
           f. HAL_ADC_Start_DMA() — continuous ADC scan to adc_buffer[]
           g. Probe OLED (0x78) — SSD1306_Init() + splash "POWERSTAGE INIT"
           h. DisplayScheduler_Init()
           i. Transition → APP_STATE_RUNNING
```

---

## 8. EEPROM Config Layout (I2C address 0xA0)

Stored at page 1, offset 0. Total size: **24 bytes**, packed.

| Offset | Size | Field | Default |
|---|---|---|---|
| 0 | 2 | `magic` (0xAB12) | 0xAB12 |
| 2 | 1 | `fan_default_mode` | 0 (OFF) |
| 3 | 1 | `fan_default_duty` | 50 % |
| 4 | 1 | `fan_min_duty` | 20 % |
| 5 | 1 | `fan_auto_on_temp` | 50 °C |
| 6 | 1 | `fan_auto_off_temp` | 45 °C |
| 7 | 1 | `hs_default_state` | 0x1F (all enabled) |
| 8–17 | 10 | `oc_threshold_mA[5]` | {5000, 2000, 5000, 5000, 5000} mA |
| 18–19 | 2 | `uv_V24_mV` | 20 000 mV |
| 20–21 | 2 | `uv_VCAP_mV` | 20 000 mV |
| 22–23 | 2 | `uv_V12_mV` | 10 000 mV |

A `magic` mismatch triggers `LoadDefault()` and overwrites the EEPROM.

---

## 9. Hardware Rail Index

| Index | Name | TPS2493 | Sense R | EN GPIO | Notes |
|---|---|---|---|---|---|
| 0 | RAIL_AUX | Yes | 3 mΩ | EN_AUX (PA) | — |
| 1 | RAIL_LED | Yes | 3 mΩ | EN_LED (PD) | LED supply |
| 2 | RAIL_DRIVE | Yes | 3 mΩ | EN_DRIVE (PD) | Motor drive |
| 3 | RAIL_CAP | Yes | 3 mΩ | EN_CAP (PB) | Delayed at boot |
| 4 | RAIL_SBC | Yes | 3 mΩ | *(none)* | SBC — no EN GPIO |
