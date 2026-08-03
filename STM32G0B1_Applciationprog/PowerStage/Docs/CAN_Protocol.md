# PowerStage — CAN Bus Protocol Reference

**Device:** STM32G0B1 PowerStage Controller
**MCU Peripherals:** FDCAN1 (internal bus) + FDCAN2 (host/relay bus)
**Nominal Baud Rate:** 500 kbps (both buses)
**Frame Format:** 11-bit Standard ID, DLC <= 8, Classic CAN
**ID block:** `0x130-0x15F` (CANopen-safe reserved gap)
**DBC File:** `PowerStage.dbc` (combined bus DBC at `Docs/Fabrica_Bus.dbc`)

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
   test tool        | PC2=RX    |    500 kbps
                    | PC3=TX    |
                    +-----------+
                         |  relay (bidirectional, no filter)
                    +-----------+
   Other nodes <--->| FDCAN1    |    CAN1 (internal bus)
   on internal bus  | PB8=RX    |    500 kbps
                    | PB9=TX    |    (OpenBLT bootloader primary)
                    +-----------+
```

| Bus | Peripheral | GPIO | Role |
|---|---|---|---|
| CAN1 (internal) | FDCAN1 / `canHandle` | PB8 (RX), PB9 (TX) | Primary bus — bootloader + application. Commands processed + broadcasts sent here. |
| CAN2 (host) | FDCAN2 / `hfdcan2` | PC2 (RX), PC3 (TX) | Host interface — relay gateway. All frames mirrored to/from CAN1. |

**Relay behaviour:**

- **CAN2 RX -> CAN1 TX:** Any frame received on the host bus is forwarded raw to the internal bus AND parsed for commands. Any frame format (standard, extended, classic, FD) is forwarded as-is.
- **CAN1 RX -> CAN2 TX:** Same direction in reverse.
- **Broadcasts:** All periodic broadcasts sent on BOTH CAN1 and CAN2 simultaneously via `CAN_SendAll()`.
- **No filter:** All CAN IDs are accepted on both buses. Filtering is done in software.

---

## 3. Bootloader Coexistence

PowerStage runs the OpenBLT bootloader. The **same CAN ID `0x130`** drives both:
1. Application "enter bootloader" trigger — running app sees `byte[0]=0xFF, dlc=2` on `0x130` and calls `NVIC_SystemReset()`.
2. Bootloader XCP CONNECT — after reset, OpenBLT listens on `0x130` and accepts the same XCP CONNECT frame (`byte[0]=0xFF, dlc=2`).

The host therefore issues exactly **one** XCP CONNECT frame to flash the device, regardless of whether the app or the bootloader is currently running. The bootloader replies on `0x131`.

This matches `STM32G0B1_Bootloader/G0B1_PowerStage_Boot/App/blt_conf.h`:
```c
#define BOOT_COM_CAN_RX_MSG_ID  (0x130)
#define BOOT_COM_CAN_TX_MSG_ID  (0x131)
```

---

## 4. Encoding Conventions

| Type | Encoding | DBC notation | Scale to engineering unit |
|---|---|---|---|
| `uint8_t` | Intel byte order | `@1+` | x 1 |
| `uint16_t` voltage/current | Big-endian (Motorola) | `@0+` | x 0.001 -> V or A |
| `uint16_t` threshold | Big-endian (Motorola) | `@0+` | x 0.001 -> V or A |
| `int16_t` temperature | Big-endian signed (Motorola) | `@0-` | x 0.1 -> C |
| bitmask | Intel byte order | `@1+` | x 1, bit-decoded |

---

## 5. CAN ID Map

```
Direction  | CAN ID | Name              | DLC | Sender      | Cycle
-----------+--------+-------------------+-----+-------------+-------
RX (boot)  | 0x130  | DEVICE_ADDR       |  2  | Master      | Event
TX (boot)  | 0x131  | PS_Bootloader_TX  |  -  | PowerStage  | Event (XCP)
RX (cmd)   | 0x140  | CMD_FAN           |  5  | Master      | Event
RX (cmd)   | 0x141  | CMD_HS            |  5  | Master      | Event
RX (cmd)   | 0x142  | CMD_OC            |  4  | Master      | Event
RX (cmd)   | 0x143  | CMD_EEPROM        |  1  | Master      | Event
RX (cmd)   | 0x144  | CMD_UV            |  6  | Master      | Event
RX (cmd)   | 0x145  | CMD_CTRL          |  2  | Master      | Event
RX (cmd)   | 0x146  | CMD_PAGE_DWELL    |  3  | Master      | Event
RX (cmd)   | 0x147  | CMD_BAT_CFG       |  1  | Master      | Event
TX (bcast) | 0x150  | BCAST_HS_STATE    |  5  | PowerStage  | 500 ms
TX (bcast) | 0x151  | BCAST_HS_CURR_A   |  8  | PowerStage  | 500 ms
TX (bcast) | 0x152  | BCAST_VOLTAGE    |  8  | PowerStage  | 500 ms
TX (bcast) | 0x153  | BCAST_FAN         |  7  | PowerStage  | 500 ms
TX (bcast) | 0x154  | BCAST_EEPROM      |  8  | PowerStage  | 500 ms
TX (bcast) | 0x155  | BCAST_HS_CURR_B   |  4  | PowerStage  | 500 ms
TX (bcast) | 0x156  | BCAST_UV          |  6  | PowerStage  | 500 ms
TX (bcast) | 0x157  | BCAST_OC_CFG_A    |  8  | PowerStage  | 500 ms
TX (bcast) | 0x158  | BCAST_OC_CFG_B    |  2  | PowerStage  | 500 ms
TX (bcast) | 0x159  | BCAST_IO_STATUS   |  3  | PowerStage  | 500 ms
TX (bcast) | 0x15A  | BCAST_BATTERY_CFG |  8  | PowerStage  | 500 ms
```

**Note:** All TX broadcasts and RX commands are mirrored on both CAN1 and CAN2 buses (when relay is enabled).

---

## 6. Message Descriptions

### 0x130 — DEVICE_ADDR / Bootloader RX (RX, DLC = 2)

System-level device commands. Also the bootloader RX after reset.

| Byte | Signal | Value | Action |
|---|---|---|---|
| 0 | `Device_Command` | `0xFF` | Software reset -> re-enter OpenBLT bootloader |
| 1 | reserved | any | (XCP CONNECT carries node-id here, ignored by app) |

---

### 0x140 — CMD_FAN (RX, DLC = 5)

Set fan operating mode, manual duty cycle, and AUTO tuning.

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `Fan_Mode` | 0 = OFF, 1 = ON_MANUAL, 2 = AUTO | Operating mode |
| 1 | `Fan_Duty` | 0-100 | Duty cycle %. Only applied when Mode = 1 |
| 2 | `Fan_Min_Duty` | 0-100 | Minimum duty for a *running* fan (anti-stall). 0 is never clamped - stop is always honoured |
| 3 | `Fan_Auto_On_Temp` | 0-150 | AUTO turns ON at or above this C |
| 4 | `Fan_Auto_Off_Temp` | 0-150 | AUTO turns OFF below this C |

**Two forms.** `DLC = 2` sets mode and duty only and is unchanged from earlier
firmware, so existing hosts keep working. `DLC = 5` additionally sets the AUTO
tuning, which before was reachable only by reflashing the EEPROM defaults.

**Tuning is applied as a group, or not at all.** Bytes 2-4 take effect only when
`Fan_Auto_On_Temp > Fan_Auto_Off_Temp`. An inverted or equal pair would make the
hysteresis latch on its first crossing and never release, leaving the fan stuck
on, so the firmware rejects the whole group rather than half-applying it.

**Live, not saved.** Values set here take effect immediately but are not
persisted; send `CMD_EEPROM` (0x143) to commit them. Read them back from
`BCAST_FAN` (0x153) bytes 4-6, *not* from `BCAST_EEPROM` (0x154), which echoes
the saved config and will still show the old values until you save.

**AUTO mode behaviour:** Fan turns ON at full speed when temperature >= `Fan_Auto_On_Temp` and turns OFF when temperature < `Fan_Auto_Off_Temp` (hysteresis). In AUTO the duty is owned by the temperature controller, so `Fan_Duty` is ignored.

---

### 0x141 — CMD_HS (RX, DLC = 5)

Enable or disable individual hot-swap rails.

| Byte | Signal | Values | Rail |
|---|---|---|---|
| 0 | `HS_Cmd_AUX` | 0 = DISABLE, 1 = ENABLE | AUX |
| 1 | `HS_Cmd_LED` | 0 = DISABLE, 1 = ENABLE | LED |
| 2 | `HS_Cmd_DRIVE` | 0 = DISABLE, 1 = ENABLE | DRIVE |
| 3 | `HS_Cmd_CAP` | 0 = DISABLE, 1 = ENABLE | CAP |
| 4 | `HS_Cmd_SBC` | *(ignored)* | SBC has no MCU-driven EN line on this board (permanently enabled by hardware). The byte is preserved for backward compatibility but the firmware skips it. |

---

### 0x142 — CMD_OC (RX, DLC = 4)

Configure per-rail software overcurrent thresholds or reset latched faults.

| Byte | Signal | Description |
|---|---|---|
| 0 | `OC_Rail_Mask` | Target rail bitmask (bit0=AUX...bit4=SBC). `0xFF` = all rails |
| 1 | `OC_Cmd` | `0x01` = SET_THRESHOLD, `0x02` = RESET_FAULT |
| 2-3 | `OC_Threshold_mA` | New threshold in mA, big-endian uint16. Applied when `OC_Cmd = 0x01` |

---

### 0x143 — CMD_EEPROM (RX, DLC = 1)

EEPROM config operations.

| Byte | Signal | Value | Action |
|---|---|---|---|
| 0 | `EEPROM_Cmd` | `0x01` = SAVE_CONFIG | Snapshot live config -> EEPROM |
| 0 | `EEPROM_Cmd` | `0x02` = LOAD_DEFAULT | Restore factory defaults, overwrite EEPROM |

---

### 0x144 — CMD_UV (RX, DLC = 6)

Set undervoltage trip thresholds for all three voltage rails.
Values are `uint16_t mV` big-endian.

| Bytes | Signal | Default | Description |
|---|---|---|---|
| 0-1 | `UV_Threshold_V24_mV` | 20000 | V24 UV trip point (mV) |
| 2-3 | `UV_Threshold_VCAP_mV` | 20000 | VCAP UV trip point (mV) |
| 4-5 | `UV_Threshold_V12_mV` | 10000 | V12 UV trip point (mV) |

---

### 0x145 — CMD_CTRL (RX, DLC = 2)

Control V_LED_PWR output and CAN relay enable/disable.

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `V_LED_PWR_Cmd` | 0 = OFF, 1 = ON | Controls V_LED_PWR output (PC0) |
| 1 | `CAN_Relay_Enable` | 0 = DISABLE, 1 = ENABLE | Controls CAN1<->CAN2 frame forwarding |

---

### 0x146 — CMD_PAGE_DWELL (RX, DLC = 3)

Set the OLED dwell time for each of the three rotating pages. Each byte is
in units of 500 ms scheduler ticks: `1` = 0.5 s, `10` = 5 s, `255` = 127.5 s.
A value of `0` resets that page to the firmware default (currently 10 ticks).

| Byte | Signal | Page |
|---|---|---|
| 0 | `Page_Dwell_Overview`     | Page 0 — System Overview |
| 1 | `Page_Dwell_RailStatus`   | Page 1 — Rail Status |
| 2 | `Page_Dwell_FaultDetail`  | Page 2 — Fault Detail |

Live values can be read back via `BCAST_EEPROM`-class flow — they are also
echoed in CMD_PAGE_DWELL form by the host's own DBC. Send `CMD_EEPROM 0x01`
afterward to persist the new dwell into EEPROM so it survives reboots.

**Example — quick scan (1 s overview, 0.5 s on each fault page):**
```
ID: 0x146  DLC: 3  Data: 02 01 01
```

**Example — only the overview page (10 s overview, 1 tick on others):**
```
ID: 0x146  DLC: 3  Data: 14 01 01
```

---

### 0x147 — CMD_BAT_CFG (RX, DLC = 1)

Set the SOC-low warning threshold (% SOC). When the filtered SOC falls
strictly below the threshold:

  - `BCAST_VOLTAGE` byte 6 bit 3 (`UV_FAULT_SOC_LOW`) is set every cycle.
  - `display_data.error_code` OR's `ERR_BAT_LOW`.
  - OLED Page 0 row 1 blinks the `SOC:nnn` slice on/off at 1 Hz (the V24
    half stays solid).

| Byte | Signal | Values | Description |
|---|---|---|---|
| 0 | `Bat_Low_SOC_Threshold_pct` | 0..100 | `0` disables the warning entirely. `1..100` trips the warning when filtered SOC sits below this value. Factory default is `BATTERY_LOW_SOC_PCT` (15). |

Send `CMD_EEPROM 0x01` afterwards to persist the new threshold. The
threshold survives reboots through `Config.bat_low_soc_pct`.

**Example — set warning to 25 %:**
```
ID: 0x147  DLC: 1  Data: 19
```

**Example — disable warning entirely:**
```
ID: 0x147  DLC: 1  Data: 00
```

---

### 0x150 — BCAST_HS_STATE (TX, DLC = 5)

Periodic broadcast of all five hot-swap rail status bitmasks.
Bit mapping for all masks: **bit 0 = RAIL_AUX, bit 1 = RAIL_LED, bit 2 = RAIL_DRIVE, bit 3 = RAIL_CAP, bit 4 = RAIL_SBC**

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `HS_Enable_Mask` | uint8 bitmask | 1 = EN GPIO asserted (rail enabled) |
| 1 | `HS_Fault_Mask` | uint8 bitmask | 1 = TPS2493 hardware FLT pin asserted |
| 2 | `HS_PGood_Mask` | uint8 bitmask | 1 = TPS2493 PGOOD pin asserted |
| 3 | `HS_OC_Warn_Mask` | uint8 bitmask | 1 = software OC warning threshold exceeded |
| 4 | `HS_OC_Fault_Mask` | uint8 bitmask | 1 = hardware OC fault latched |

---

### 0x151 — BCAST_HS_CURR_A (TX, DLC = 8)

Current measurements for BAT, CAP, SBC, DRIVE rails.
All values are `uint16_t mA` big-endian. **Scale x 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0-1 | `I_BAT_mA` | 0-50000 | mA |
| 2-3 | `I_CAP_mA` | 0-50000 | mA |
| 4-5 | `I_SBC_mA` | 0-50000 | mA |
| 6-7 | `I_DRIVE_mA` | 0-50000 | mA |

---

### 0x152 — BCAST_VOLTAGE (TX, DLC = 8)

Bus voltage measurements, UV fault status, and 6S battery SOC.
Voltage values are `uint16_t mV` big-endian. **Scale x 0.001 to get Volts.**

| Bytes | Signal | Range | Unit | Description |
|---|---|---|---|---|
| 0-1 | `V24_mV` | 0-30000 | mV | 24V bus |
| 2-3 | `VCAP_mV` | 0-30000 | mV | Capacitor bus |
| 4-5 | `V12_mV` | 0-15000 | mV | 12V bus |
| 6 | `UV_Fault_Mask` | 0-15 | — | bit 0 = V24 UV, bit 1 = VCAP UV, bit 2 = V12 UV, bit 3 = SOC LOW (battery_soc_pct < `BATTERY_LOW_SOC_PCT`, default 15 %). bits 4-7 reserved. |
| 7 | `Battery_SOC_pct` | 0-100 | % | 6S Li-ion/Li-Po pack state-of-charge. 0 % = 19.6 V cutoff (3.27 V/cell), 100 % = 25.2 V (4.20 V/cell). Estimated by `Battery_EstimateSOC_pct()` with IR compensation (default 200 mΩ pack resistance) — see `battery.c`. |

---

### 0x153 — BCAST_FAN (TX, DLC = 7)

Periodic broadcast of fan state, NTC temperature, and the live AUTO tuning.

| Bytes | Signal | Type | Scale | Unit | Description |
|---|---|---|---|---|---|
| 0 | `Fan_Mode_State` | uint8 | x 1 | — | 0=OFF 1=ON_MANUAL 2=AUTO |
| 1 | `Fan_Duty_State` | uint8 | x 1 | % | **Applied** duty cycle 0-100 |
| 2-3 | `Temperature_C` | int16 big-endian | x 0.1 | C | NTC temperature x 10. `0x00FD` = 25.3 C |
| 4 | `Fan_Min_Duty_State` | uint8 | x 1 | % | Live anti-stall floor |
| 5 | `Fan_Auto_On_Temp_State` | uint8 | x 1 | C | Live AUTO on threshold |
| 6 | `Fan_Auto_Off_Temp_State` | uint8 | x 1 | C | Live AUTO off threshold |

`Fan_Duty_State` is the duty actually programmed into the timer, not the last
value commanded. In AUTO the controller sets it from temperature, and when the
fan is off it reads 0.

Bytes 4-6 report the tuning **currently in force**. `BCAST_EEPROM` (0x154)
reports the **saved** config instead, so a threshold set over CAN but not yet
committed appears here and not there. Bytes 0-3 are unchanged from earlier
firmware.

---

### 0x154 — BCAST_EEPROM (TX, DLC = 8)

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

### 0x155 — BCAST_HS_CURR_B (TX, DLC = 4)

Current measurements for AUX and LED rails.
Values are `uint16_t mA` big-endian. **Scale x 0.001 to get Amperes.**

| Bytes | Signal | Range | Unit |
|---|---|---|---|
| 0-1 | `I_AUX_mA` | 0-50000 | mA |
| 2-3 | `I_LED_mA` | 0-50000 | mA |

---

### 0x156 — BCAST_UV (TX, DLC = 6)

Periodic echo of the currently active UV thresholds.

| Bytes | Signal | Unit |
|---|---|---|
| 0-1 | `UV_Active_V24_mV` | mV |
| 2-3 | `UV_Active_VCAP_mV` | mV |
| 4-5 | `UV_Active_V12_mV` | mV |

---

### 0x157 — BCAST_OC_CFG_A (TX, DLC = 8)

Periodic echo of per-rail OC thresholds (rails AUX, LED, DRIVE, CAP).
Values are `uint16_t mA` big-endian.

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0-1 | `OC_Threshold_AUX_mA` | 5000 |
| 2-3 | `OC_Threshold_LED_mA` | 2000 |
| 4-5 | `OC_Threshold_DRIVE_mA` | 5000 |
| 6-7 | `OC_Threshold_CAP_mA` | 5000 |

---

### 0x158 — BCAST_OC_CFG_B (TX, DLC = 2)

Periodic echo of OC threshold for SBC rail (continuation of frame A).
**Always reads 0** on this board: the SBC rail has no MCU-driven EN line so
software OC has nothing to cut, and the firmware refuses to update the
SBC threshold via `CMD_OC`. The hardware FLT pin is still read into
`HS_Fault_Mask` for telemetry only.

| Bytes | Signal | Default (mA) |
|---|---|---|
| 0-1 | `OC_Threshold_SBC_mA` | 0 *(disabled — read-only)* |

---

### 0x159 — BCAST_IO_STATUS (TX, DLC = 3)

Periodic broadcast of GPIO and relay status.

| Byte | Signal | Type | Description |
|---|---|---|---|
| 0 | `SW_Pin_State` | uint8 | Current state of SW pin (PC1). 0=LOW, 1=HIGH |
| 1 | `V_LED_PWR_State` | uint8 | Current V_LED_PWR output state. 0=OFF, 1=ON |
| 2 | `CAN_Relay_State` | uint8 | CAN relay enabled. 0=disabled, 1=enabled |

---

### 0x15A — BCAST_BATTERY_CFG (TX, DLC = 8)

Static battery configuration — lets the host display the SOC reference points
and IR-compensation parameter without hardcoding them. Values are compile-time
constants from `battery.h` (`BATTERY_CUTOFF_MV`, `BATTERY_FULL_MV`,
`BATTERY_INT_R_MILLIOHM`).

| Bytes | Signal | Type | Default | Description |
|---|---|---|---|---|
| 0-1 | `Bat_Cutoff_mV` | uint16 BE | 19600 | 0 % SOC reference (3.27 V/cell × 6 = 19.6 V) |
| 2-3 | `Bat_Full_mV` | uint16 BE | 25200 | 100 % SOC reference (4.20 V/cell × 6 = 25.2 V) |
| 4-5 | `Bat_Rint_mOhm` | uint16 BE | 200 | Pack internal resistance for IR compensation: V_OCV = V_meas + I_BAT × R_int |
| 6 | `Bat_Cell_Count` | uint8 | 6 | Series cell count |
| 7 | *(reserved)* | — | — | — |

The SOC value reported in `BCAST_VOLTAGE.Battery_SOC_pct` is post-filter:
the raw OCV→SOC lookup is run through an EMA (≈1.6 s time constant) and an
asymmetric hysteresis stage (drops at ≥1 % delta, rises only at ≥3 % delta)
so the displayed value moves only when degradation/recovery is confirmed.

---

## 7. Fault & Error Code Bitmask

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

## 8. EEPROM Config Layout (I2C address 0xA0)

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

## 9. Hardware Rail Index

| Index | Name | TPS2493 | Sense R | EN GPIO | Notes |
|---|---|---|---|---|---|
| 0 | RAIL_AUX | Yes | 3 mR | EN_AUX (PA10) | -- |
| 1 | RAIL_LED | Yes | 3 mR | EN_LED (PD1) | LED supply |
| 2 | RAIL_DRIVE | Yes | 3 mR | EN_DRIVE (PD4) | Motor drive |
| 3 | RAIL_CAP | Yes | 3 mR | EN_CAP (PB3) | Delayed at boot |
| 4 | RAIL_SBC | Yes | 3 mR | *(none)* | SBC -- no EN GPIO |

---

## 10. Quick-Reference: CAN Commands for Testing

Send these frames from your CAN tool (on either CAN1 or CAN2) to control the PowerStage:

**Reset to bootloader:**
```
ID: 0x130  DLC: 2  Data: FF 00
```

**Enable all HS rails:**
```
ID: 0x141  DLC: 5  Data: 01 01 01 01 01
```

**Disable all HS rails:**
```
ID: 0x141  DLC: 5  Data: 00 00 00 00 00
```

**Fan ON manual 75%:**
```
ID: 0x140  DLC: 2  Data: 01 4B
```

**Fan AUTO mode:**
```
ID: 0x140  DLC: 2  Data: 02 00
```

**Fan OFF:**
```
ID: 0x140  DLC: 2  Data: 00 00
```

**Set all OC thresholds to 3A (3000 mA = 0x0BB8):**
```
ID: 0x142  DLC: 4  Data: FF 01 0B B8
```

**Reset all OC faults:**
```
ID: 0x142  DLC: 4  Data: FF 02 00 00
```

**Set UV thresholds (V24=18V, VCAP=18V, V12=9V):**
```
ID: 0x144  DLC: 6  Data: 46 50 46 50 23 28
                          ^18000mV ^18000mV ^9000mV
```

**Save config to EEPROM:**
```
ID: 0x143  DLC: 1  Data: 01
```

**Load factory defaults:**
```
ID: 0x143  DLC: 1  Data: 02
```

**Turn on V_LED_PWR, relay enabled:**
```
ID: 0x145  DLC: 2  Data: 01 01
```

**Turn off V_LED_PWR, disable CAN relay:**
```
ID: 0x145  DLC: 2  Data: 00 00
```
