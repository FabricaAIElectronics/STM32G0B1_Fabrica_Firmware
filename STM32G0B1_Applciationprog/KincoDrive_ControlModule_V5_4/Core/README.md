# Actuation IO Distribution Board — Embedded Firmware

Embedded firmware for a custom STM32G0B1-based power distribution and IO board.  
Manages high-side power switching, overcurrent/overvoltage protection, fan control,
temperature monitoring, and EEPROM-stored configuration — all commanded and monitored
over a 500 kbps CAN 2.0B bus.

---

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [Project Structure](#project-structure)
- [Build & Flash](#build--flash)
- [Boot Sequence](#boot-sequence)
- [Module Descriptions](#module-descriptions)
  - [Power Electronics](#power-electronics)
  - [CAN Handler](#can-handler)
  - [Error Manager](#error-manager)
  - [EEPROM Driver](#eeprom-driver)
  - [Fan PWM](#fan-pwm)
  - [Endstop / E-Stop](#endstop--e-stop)
  - [Core System](#core-system)
- [CAN Bus Protocol](#can-bus-protocol)
  - [Commands (Host → MCU)](#commands-host--mcu)
  - [Responses (MCU → Host)](#responses-mcu--host)
  - [Periodic Broadcasts](#periodic-broadcasts-mcu--bus-every-500-ms)
  - [Error Dump Frames](#error-dump-frames)
  - [CAN ID Map](#can-id-map)
- [EEPROM Configuration](#eeprom-configuration)
  - [Config Struct Layout](#config-struct-layout)
  - [Setting Selectors](#setting-selectors-for-can-id-0x200)
  - [Field Validation Ranges](#field-validation-ranges)
- [Protection System](#protection-system)
  - [Overcurrent Protection](#overcurrent-protection)
  - [Overvoltage Protection](#overvoltage-protection)
  - [Undervoltage Protection](#undervoltage-protection)
  - [Error State Machine](#error-state-machine)
  - [Recovery Procedure](#recovery-procedure)
- [ADC Channel Mapping](#adc-channel-mapping)
- [CAN Bit Timing](#can-bit-timing)
- [Known Issues & Notes](#known-issues--notes)
- [Version History](#version-history)

---

## Hardware Overview

| Item                | Detail                                      |
|---------------------|---------------------------------------------|
| MCU                 | STM32G0B1 (Cortex-M0+, 60 MHz)             |
| CAN Transceiver     | FDCAN1 in Classic CAN 2.0B mode, 500 kbps   |
| High-Side Switches  | 3× TPS2HB16-Q1 (Drive, Extruder, Scrubbing) |
| Current Sensing     | TPS2493 sense amp, gain = 48, Rsense = 3 mΩ |
| Bus Voltage Sensing | Resistor divider (200 kΩ / 22 kΩ), ratio 10.09:1 |
| 12 V Buck           | Switchable via GPIO (VBUCK_CTRL)             |
| Temperature         | 6× PTC thermistors, 100 kΩ pull-up, B=3950  |
| Fan Outputs         | 5× PWM channels (TIM3/TIM14/TIM16)          |
| EEPROM              | I²C, 64-byte pages, 512 pages               |
| Endstops            | 6× digital inputs + E-Stop                  |
| Bootloader          | OpenBLT (CAN entry via ID 0x667)             |

---

## Project Structure

```
Actuation_IO_Distribution_Board_Embedded/
├── Core/
│   ├── Inc/                    # Application headers
│   │   ├── CAN_Handler.h       # CAN command IDs, dispatch API
│   │   ├── Power_Electronic.h  # HS power, ADC, voltage/current API
│   │   ├── error_manager.h     # Error state machine, error codes
│   │   ├── eeprom_driver.h     # EEPROM R/W, Config struct
│   │   ├── Fan_PWM.h           # Fan speed control
│   │   ├── Endstop.h           # Endstop / E-Stop handling
│   │   └── Core_System.h       # Top-level state machine
│   └── Src/                    # Application source
│       ├── CAN_Handler.c       # CAN dispatch, ring buffer, broadcast
│       ├── Power_Electronic.c  # HS power, ADC reads, shutdown protection
│       ├── error_manager.c     # Error logging, state enforcement, recovery
│       ├── eeprom_driver.c     # I²C EEPROM driver, config sanitization
│       ├── Fan_PWM.c           # PWM duty cycle control
│       ├── Endstop.c           # GPIO endstop reads, E-Stop logic
│       ├── Core_System.c       # Main state machine, periodic tasks
│       ├── fdcan.c             # CubeMX-generated FDCAN init + filter
│       ├── main.c              # HAL init, main loop
│       └── stm32g0xx_it.c      # Interrupt handlers
├── Drivers/                    # STM32 HAL drivers (CubeMX-generated)
└── README.md                   # ← You are here
```

---

## Notes to Software integration IMPT!!!!!!

Anything with CAN_Handler or CAN_Broadcast is a demo styled meant for Low level testing verification and is meant to be replaced with actual CANOpen integrations.

| Classification | Meaning | Action Required |
|---------------|---------|-----------------|
| 🔴 **PRESERVE** | Hardware-pinned code. Talks directly to GPIO, ADC, DMA, I²C, timers. Changing these will break the electrical interface to the custom PCB. | **Do not modify function signatures or internal HW register access.** You may rename, move into a HAL wrapper, or add a thin adapter — but the underlying GPIO/peripheral operations must remain identical. |
| 🟡 **ADAPT** | Abstracted low level functions for advanced functionality | Can edit after reading demo but feature should exist to ensure deterministic behaviour of module |
| 🟢 **REWRITE** | Demo CAN transport. Purpose-built for bench testing with raw CAN frames. | **Replace entirely** with CANopen stack (e.g. CANopenNode). The dispatch table, ring buffer, broadcast packer, and all `CAN_Handler_*` functions are disposable. |


Below is a brief decription of stuff that must be preserved.
### PRESERVE — Hardware Abstraction Layer
| File | What it controls | Why it must be preserved |
|------|-----------------|------------------------|

| `Power_Electronic.c/.h` | 3× HS switch enable/disable, 4× current ADC, 2× voltage ADC, 6× thermistor ADC, 12V buck GPIO | GPIO pin assignments, ADC DMA buffer layout, voltage divider math, current sense gain — all tied to PCB traces |
| `Fan_PWM.c/.h` | 5× fan PWM outputs | Timer channel → pin assignments are PCB-specific |
| `Endstop.c/.h` | 6× endstop + E-Stop GPIO reads | Pin assignments are PCB-specific |
| `eeprom_driver.c/.h` | I²C EEPROM read/write/erase, Config struct, sanitization | I²C address (0xA0), page size (64B), page count (512) are chip-specific. Shows you how to interact with the EEPROM |
| `fdcan.c` | FDCAN1 peripheral init, bit timing, **accept-all filter** | Bit timing (Prescaler=1, Tseg1=95, Tseg2=24, SJW=12) is validated on this bus. Filter config must route frames to Rx FIFO 0. |
| `stm32g0xx_it.c` | DMA complete ISR for ADC, FDCAN Rx ISR | DMA IRQ must stay connected to ADC1; FDCAN IRQ routing is needed for any CAN stack.

### PRESERVE - Boot Order
The boot order mentioned later must be preserved as the sequence of IO bring up is important

## Build & Flash

### Prerequisites

- **STM32CubeIDE** 1.14+ (or any ARM GCC toolchain)
- **STM32CubeMX** (for peripheral reconfiguration only)
- **OpenBLT** host tool (for CAN firmware updates)
- **CAN adapter** (e.g., PCAN-USB)

### Build

1. Open `Actuation_IO_Distribution_Board_Embedded` as an STM32CubeIDE project
2. Build the project (`Ctrl+B`)
3. The output binary is in `Debug/` or `Release/`

### Flash via SWD

Connect ST-Link to the SWD header and flash from STM32CubeIDE (`F11`).

### Flash via CAN Bootloader

1. Power the board
2. Send CAN frame: `ID=0x667, DLC=1, Data=[0xFF]`
3. MCU resets into OpenBLT bootloader
4. Use OpenBLT Microboot utility to flash the `.srec` file over CAN

---

## Boot Sequence

```
Power On / Reset
    │
    ├── HAL_Init()
    ├── SystemClock_Config()          → 60 MHz from HSE + PLL
    ├── MX_GPIO_Init()
    ├── MX_DMA_Init()
    ├── MX_ADC1_Init()
    ├── MX_FDCAN1_Init()              → 500 kbps, accept-all filter
    ├── MX_I2C1_Init()
    ├── MX_TIMx_Init()                → PWM timers for fans
    │
    ├── Pre_CAN_Handler_Init()        → Vector table remap (for bootloader)
    ├── EEPROM_Init()                 → Read config, sanitize, cache
    ├── Calibrate_ADC1() + Start DMA  → Continuous ADC conversion
    ├── HAL_FDCAN_Start()             → CAN peripheral goes online
    ├── CAN_Handler_Init()            → Register all command handlers
    ├── Error_Manager_Init()          → Clear error log, set STATE_NORMAL
    │
    └── Main Loop (forever)
          ├── CAN_Handler_Dispatch_Process_One()   → Process 1 queued CAN frame
          ├── CoreSystem_PeriodicTasks()            → Broadcast, protection, recovery
          └── CoreSystem_TOP()                      → State-dependent logic
```

---

## Module Descriptions

### Power Electronics

**File:** `Power_Electronic.c / .h`

Modules
HS_MODULE_DRIVE
HS_MODULE_EXTRUDER
HS_MODULE_SCRUBBING

Manages 3 high-side power switches (Drive, Extruder, Scrubbing), a 12V buck
converter, ADC readings for 4 current channels, 2 voltage channels, and 6
thermistors. Includes `Shutdown_Protection()` which runs every loop iteration
to check overcurrent and overvoltage thresholds.

| Function | Description |
|----------|-------------|
| `Enable_HighSide_Power_Module(module, timeout)` | Enable one HS switch, wait for Power Good |
| `Disable_HighSide_Power_Module(module, timeout)` | Disable one HS switch |
| `Read_HighSide_Module_Current_mA(module, *mA)` | Read per-module current via ADC |
| `Read_24V_Voltage_1DP(*v)` | Read 24V bus voltage (1 decimal place, e.g. 245 = 24.5V) |
| `Shutdown_Protection()` | Periodic OC/OV check, auto-disables on fault |
| `Shutdown_Protection_ResetState()` | Re-arm edge detection after recovery |

### CAN Handler

**File:** `CAN_Handler.c / .h`

Interrupt-driven CAN receive into a 16-deep ring buffer, dispatched in the main
loop via a 2048-entry function pointer table indexed by CAN ID. Also handles
periodic 500 ms broadcast of system telemetry.

| Function | Description |
|----------|-------------|
| `can_register_handler(cmd, handler)` | Register a callback for a CAN ID |
| `CAN_Handler_Dispatch_Process_One()` | Dequeue and dispatch one CAN frame |
| `CAN_Handler_Broadcast()` | Send telemetry on IDs 0x600–0x603 |
| `FDCAN_SendFrame(id, data, len)` | Transmit an extended (29-bit) CAN frame |

### Error Manager

**File:** `error_manager.c / .h`

Maintains a ring-buffer error log (up to 32 entries) and a system state machine
(`NORMAL → WARNING → ERROR → RECOVERY → NORMAL`). Enforces power lockout
during ERROR and RECOVERY states.

| Function | Description |
|----------|-------------|
| `Error_Manager_Log(source, severity, detail)` | Log an error, auto-escalate state |
| `Error_Manager_IsPowerAllowed()` | Returns `true` only in NORMAL or WARNING |
| `Error_Manager_EnforceState()` | Force-disable modules if in ERROR/RECOVERY |
| `Error_Manager_AttemptRecovery()` | Periodic recovery validation (5 clean reads) |

### EEPROM Driver

**File:** `eeprom_driver.c / .h`

I²C EEPROM driver with a cached `Config` struct. On boot, reads config from
page 0 and sanitizes all fields against valid ranges. If any field is out of
range (e.g. `0xFFFF` from uninitialized EEPROM), it's replaced with a safe
default and rewritten.

### Fan PWM

**File:** `Fan_PWM.c / .h`

Controls 5 fan channels via hardware PWM timers. Speed is set as a percentage
(0–100%) via CAN command `0x140`.

### Endstop / E-Stop

**File:** `Endstop.c / .h`

Reads 6 endstop GPIO inputs and the E-Stop input. Packs state into CAN
broadcast frame `0x600`.

### Core System

**File:** `Core_System.c / .h`

Top-level state machine that orchestrates periodic tasks (broadcast, protection,
recovery) and routes system state to the appropriate operating mode.

---

## CAN Bus Protocol

**Bus Speed:** 500 kbps  
**Frame Format:** CAN 2.0B Standard (11-bit ID)  
**Byte Order:** Little-endian for all multi-byte fields

### Commands (Host → MCU)

| CAN ID | Name | DLC | Payload | Description |
|--------|------|-----|---------|-------------|
| `0x060` | Set Test LED | 1 | `[any]` | Toggle PA5 LED |
| `0x110` | Set HS Drive Power | 1 | `[0/1]` | 0=OFF, 1=ON |
| `0x111` | Set HS Extruder Power | 1 | `[0/1]` | 0=OFF, 1=ON |
| `0x112` | Set HS Scrubbing Power | 1 | `[0/1]` | 0=OFF, 1=ON |
| `0x113` | Enable 12V Buck | — | — | *(reserved)* |
| `0x140` | Set Fan PWM | 2 | `[fan_num, speed%]` | fan: 1–5, speed: 0–100 |
| `0x200` | Write EEPROM Setting | 3 | `[selector, val_lo, val_hi]` | See [Setting Selectors](#setting-selectors-for-can-id-0x200) |
| `0x201` | Read EEPROM Config | 0 | *(empty)* | Triggers config dump on 0x604/0x605 |
| `0x667` | Bootloader Entry | 1 | `[0xFF]` | Resets MCU into OpenBLT bootloader |
| `0x703` | Dump Errors | 0 | *(empty)* | Triggers error dump on 0x701 |
| `0x704` | Reset Error State | 1 | `[0xAA]` | Safety key required; ERROR → RECOVERY |

### Responses (MCU → Host)

| CAN ID | Source | DLC | Payload | Meaning |
|--------|--------|-----|---------|---------|
| `0x700` | HS Power | 2 | `[cmd_lo, 0x00]` | ACK — command succeeded |
| `0x700` | HS Power | 2 | `[cmd_lo, 0xFF]` | NACK — blocked by error state |
| `0x700` | Protection | 2 | `[0x11, 0x00]` | Alert — overcurrent shutdown |
| `0x700` | Protection | 2 | `[0x12, 0x00]` | Alert — hard overvoltage shutdown |
| `0x702` | Error Reset | 2 | `[0xBB, 0x00]` | ACK — entered RECOVERY |
| `0x702` | Error Reset | 2 | `[0xAA, 0x00]` | ACK — recovery succeeded, NORMAL |
| `0x702` | Error Reset | 2 | `[0x00, 0xFF]` | NACK — bad safety key |
| `0x702` | Error Reset | 2 | `[0x01, 0xFF]` | NACK — not in ERROR state |
| `0x702` | Error Reset | 2 | `[0xEE, 0xFF]` | NACK — recovery timeout |
| `0x702` | Error Reset | 2 | `[0xCC, 0xFF]` | NACK — recovery failed (fault persists) |
| `0x604` | EEPROM Read | 8 | `[hov_lo, hov_hi, sov_lo, sov_hi, oc_lo, oc_hi, fan_lo, fan_hi]` | Config dump part 1 (all u16 LE) |
| `0x605` | EEPROM Read | 2 | `[uv_lo, uv_hi]` | Config dump part 2 (under_voltage) |
| `0x604` | EEPROM Write | 3 | `[selector, val_lo, val_hi]` | Write ACK — echoes setting + value |

### Periodic Broadcasts (MCU → Bus, every 500 ms)

| CAN ID | Name | DLC | Byte Map |
|--------|------|-----|----------|
| `0x600` | System Status | 8 | `[endstop_trig, endstop_fault, v24_lo, v24_hi, v12, prot_state, 0, 0]` |
| `0x601` | Currents | 5 | `[i24_lo, i24_hi, i_drive, i_ext, i_scrub]` |
| `0x602` | Temperatures | 8 | `[ptc1, ptc2, ptc3, ptc4, ptc5, ptc6, 0, 0]` |
| `0x603` | Fan Speeds | 5 | `[fan_dr, fan_ep, fan_eh, fan_st, fan_sf]` |

**Broadcast field encoding:**

| Field | Encoding | Decode |
|-------|----------|--------|
| `v24` | u16 LE, 0.1V units | `voltage_V = value / 10.0` |
| `v12` | u8, 0.1V units | `voltage_V = value / 10.0` |
| `i24` | u16 LE, 0.1A units | `current_A = value / 10.0` |
| `i_drive/ext/scrub` | u8, 0.1A units | `current_A = value / 10.0` |
| `ptcN` | u8, offset +40 | `temp_C = value - 40` |
| `fan_xx` | u8, 0–100 | Percentage of `fan_max_rpm` |
| `prot_state` | bitfield | `[1:0]`=DR, `[3:2]`=EXT, `[5:4]`=SC (OC), `[7:6]`=OV |

**Protection state bitfield decoding:**

| Bits | Module | Values |
|------|--------|--------|
| `[1:0]` | Drive overcurrent | 0=OK, 1=OC |
| `[3:2]` | Extruder overcurrent | 0=OK, 1=OC |
| `[5:4]` | Scrubbing overcurrent | 0=OK, 1=OC |
| `[7:6]` | Bus overvoltage | 0=OK, 1=Soft OV, 2=Hard OV |

### Error Dump Frames

Response to command `0x703`. Sent as a sequence of 8-byte frames on ID `0x701`:

| Byte | Field | Description |
|------|-------|-------------|
| 0 | Index | Error sequence number (0-based, oldest first) |
| 1 | Source | `ErrorSource_t` code (see below) |
| 2 | Severity | 0 = WARNING, 1 = CRITICAL |
| 3–4 | Detail | u16 LE — measured mV or mA at time of fault |
| 5–7 | Timestamp | u24 LE — lower 3 bytes of `HAL_GetTick()` (ms since boot) |

Terminated by: `[0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]`

**Error source codes:**

| Code | Name | Description |
|------|------|-------------|
| `0x00` | `ERR_SRC_NONE` | Generic / unspecified |
| `0x01` | `ERR_SRC_OVERCURRENT_DRIVE` | Drive module overcurrent |
| `0x02` | `ERR_SRC_OVERCURRENT_EXT` | Extruder module overcurrent |
| `0x03` | `ERR_SRC_OVERCURRENT_SCRUB` | Scrubbing module overcurrent |
| `0x04` | `ERR_SRC_OVERCURRENT_BUS` | 24V bus overcurrent |
| `0x10` | `ERR_SRC_OVERVOLTAGE_SOFT` | Soft overvoltage warning |
| `0x11` | `ERR_SRC_OVERVOLTAGE_HARD` | Hard overvoltage — all modules shut down |
| `0x12` | `ERR_SRC_UNDERVOLTAGE` | Bus undervoltage |
| `0x20` | `ERR_SRC_ENDSTOP` | Endstop fault |
| `0x21` | `ERR_SRC_ESTOP_FAULT` | E-Stop wiring fault |
| `0x22` | `ERR_SRC_ESTOP_TRIGGERED` | E-Stop activated |
| `0x30` | `ERR_SRC_THERMAL` | Thermal fault |
| `0x40` | `ERR_SRC_EEPROM` | EEPROM read/write fault |
| `0x50` | `ERR_SRC_CAN` | CAN bus fault |

### CAN ID Map

| ID | Hex | Direction | Category |
|----|-----|-----------|----------|
| 96 | `0x060` | Host → MCU | Command (LED test) |
| 272 | `0x110` | Host → MCU | Command (Drive power) |
| 273 | `0x111` | Host → MCU | Command (Extruder power) |
| 274 | `0x112` | Host → MCU | Command (Scrubbing power) |
| 275 | `0x113` | Host → MCU | Command (12V buck) |
| 320 | `0x140` | Host → MCU | Command (Fan PWM) |
| 512 | `0x200` | Host → MCU | Command (EEPROM write) |
| 513 | `0x201` | Host → MCU | Command (EEPROM read) |
| 1536 | `0x600` | MCU → Bus | Broadcast (system status) |
| 1537 | `0x601` | MCU → Bus | Broadcast (currents) |
| 1538 | `0x602` | MCU → Bus | Broadcast (temperatures) |
| 1539 | `0x603` | MCU → Bus | Broadcast (fan speeds) |
| 1540 | `0x604` | MCU → Host | Response (EEPROM config / write ACK) |
| 1541 | `0x605` | MCU → Host | Response (EEPROM under-voltage) |
| 1639 | `0x667` | Host → MCU | System (bootloader entry) |
| 1792 | `0x700` | MCU → Host | Response (HS power ACK/NACK/alerts) |
| 1793 | `0x701` | MCU → Host | Response (error dump entries) |
| 1794 | `0x702` | MCU → Host | Response (error reset ACK/NACK) |
| 1795 | `0x703` | Host → MCU | Command (dump errors) |
| 1796 | `0x704` | Host → MCU | Command (reset error state) |

---

## EEPROM Configuration

### Config Struct Layout

```c
typedef struct __attribute__((packed)) {
    uint16_t    magic;              // 0x3584 = valid config
    uint8_t     mode;               // Operating mode
    uint16_t    voltage;            // Reference voltage setting
    uint16_t    count;              // Cycle count
    uint16_t    pwm0;               // PWM channel 0 default (%)
    uint16_t    pwm1;               // PWM channel 1 default (%)
    uint16_t    hard_over_voltage;  // Hard OV threshold (mV)
    uint16_t    soft_over_voltage;  // Soft OV threshold (mV)
    uint16_t    under_voltage;      // Undervoltage threshold (mV)
    uint16_t    over_current;       // Overcurrent threshold (mA)
    uint16_t    fan_max_rpm;        // Fan max RPM setting
} Config;
```

### Setting Selectors (for CAN ID 0x200)

| Selector | Hex | Field | Default |
|----------|-----|-------|---------|
| 0 | `0x00` | `hard_over_voltage` | 28000 mV |
| 1 | `0x01` | `soft_over_voltage` | 27000 mV |
| 2 | `0x02` | `under_voltage` | 18000 mV |
| 3 | `0x03` | `over_current` | 10000 mA |
| 4 | `0x04` | `fan_max_rpm` | 5000 RPM |

### Field Validation Ranges

Applied on every boot by `EEPROM_SanitizeConfig()` and on every CAN write:

| Field | Min | Max | Additional Constraint |
|-------|-----|-----|----------------------|
| `hard_over_voltage` | 10,000 mV | 60,000 mV | — |
| `soft_over_voltage` | 10,000 mV | 60,000 mV | Must be < `hard_over_voltage` |
| `under_voltage` | 5,000 mV | 30,000 mV | Must be < `soft_over_voltage` |
| `over_current` | 500 mA | 60,000 mA | — |
| `fan_max_rpm` | 100 | 30,000 | — |
| `pwm0` / `pwm1` | 0 | 100 | — |
| `count` | 1 | 10,000 | — |

---

## Protection System

### Overcurrent Protection

- Checked every main loop iteration in `Shutdown_Protection()`
- Per-module: compares `Read_HighSide_Module_Current_mA()` against `over_current` threshold
- **On transition OK → OC:** disables that specific HS module, logs `ERR_SEV_CRITICAL`
- Edge-triggered (fires once per transition, not continuously)

### Overvoltage Protection

- Checked every main loop iteration in `Shutdown_Protection()`
- **Soft OV** (`bus_mV ≥ soft_over_voltage`): logs `ERR_SEV_WARNING`, no shutdown
- **Hard OV** (`bus_mV ≥ hard_over_voltage`): disables ALL HS modules, logs `ERR_SEV_CRITICAL`
- Edge-triggered on state transitions

### Undervoltage Protection

- Checked during recovery validation in `Error_Manager_AttemptRecovery()`
- Compares against `under_voltage` threshold
- Prevents recovery to NORMAL if bus is undervoltage

### Error State Machine

```
                  ┌──────────────────────────┐
                  │                          │
                  ▼                          │
            ┌──────────┐   Warning logged   ┌──────────┐
    Boot ──►│  NORMAL  │ ─────────────────► │ WARNING  │
            │ State: 0 │ ◄───────────────── │ State: 1 │
            └──────────┘   Warnings clear   └──────────┘
                  │                              │
                  │  Critical error              │  Critical error
                  ▼                              ▼
            ┌──────────┐                         │
            │  ERROR   │ ◄───────────────────────┘
            │ State: 2 │
            └──────────┘
                  │
                  │  CAN Reset (0x704, key=0xAA)
                  ▼
            ┌──────────┐  5 clean reads    ┌──────────┐
            │ RECOVERY │ ─────────────────►│  NORMAL  │
            │ State: 3 │                   │ State: 0 │
            └──────────┘                   └──────────┘
                  │
                  │  Fault persists or timeout (5s)
                  ▼
            ┌──────────┐
            │  ERROR   │
            │ State: 2 │
            └──────────┘
```

**Power allowed:** NORMAL and WARNING only  
**Power blocked:** ERROR and RECOVERY

### Recovery Procedure

1. Host sends `ID=0x704, Data=[0xAA]`
2. MCU enters `STATE_RECOVERY`, ACKs with `0xBB`
3. Every loop iteration, `Error_Manager_AttemptRecovery()` checks:
   - Hard overvoltage → **immediate fail** back to ERROR
   - Soft overvoltage → resets consecutive-OK counter
   - Undervoltage → **immediate fail** back to ERROR
   - Per-module overcurrent → **immediate fail** back to ERROR
4. After **5 consecutive clean reads** → clears lockout flags, resets
   `Shutdown_Protection` edge state, enters NORMAL, ACKs with `0xAA`
5. After **5 second timeout** → fails back to ERROR, NACKs with `0xEE`

---

## ADC Channel Mapping

| Index | Enum | Signal | Conversion |
|-------|------|--------|------------|
| 0 | `CURR_MON_1` | Drive module current | V / 0.144 = Amps |
| 1 | `CURR_MON_2` | Extruder module current | V / 0.144 = Amps |
| 2 | `CURR_MON_3` | Scrubbing module current | V / 0.144 = Amps |
| 3 | `CURR_MON_24V` | 24V bus current | V / 0.144 = Amps |
| 4 | `VADC_24` | 24V bus voltage | V × 10.09 = Bus V |
| 5 | `VADC_12` | 12V rail voltage | V × 10.09 = Rail V |
| 6 | `TEMP_PTC_1` | Thermistor 1 | Steinhart-Hart (B=3950) |
| 7 | `TEMP_PTC_2` | Thermistor 2 | Steinhart-Hart (B=3950) |
| 8 | `TEMP_PTC_3` | Thermistor 3 | Steinhart-Hart (B=3950) |
| 9 | `TEMP_PTC_4` | Thermistor 4 | Steinhart-Hart (B=3950) |
| 10 | `TEMP_PTC_5` | Thermistor 5 | Steinhart-Hart (B=3950) |
| 11 | `TEMP_PTC_6` | Thermistor 6 | Steinhart-Hart (B=3950) |

**Current sense formula:**  
`I (A) = V_adc / (Gain × Rsense) = V_adc / (48 × 0.003) = V_adc / 0.144`

**Voltage divider:**  
`V_bus = V_adc × (R1 + R2) / R2 = V_adc × 222 / 22 = V_adc × 10.09`

---

## CAN Bit Timing

| Parameter | Value |
|-----------|-------|
| FDCAN Clock | 60 MHz (PCLK1) |
| Prescaler | 1 |
| Time Seg 1 | 95 |
| Time Seg 2 | 24 |
| Sync Jump Width | 12 |
| Total Time Quanta | 1 + 95 + 24 = 120 |
| **Baud Rate** | 60 MHz / 120 = **500 kbps** |
| **Sample Point** | (1 + 95) / 120 = **80.0%** |

---

## Known Issues & Notes

1. **EEPROM field ordering matters:** If the `Config` struct is extended with new
   fields, the `EEPROM_SanitizeConfig()` function will auto-repair them on first
   boot with the new firmware.

2. **Thermistor B-constant** is set to 3950 — verify against your specific PTC
   datasheet and update if needed.

3. **Config needs max fan speed** Fan speed spec of the fan used for external modules need to be put in configs

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-12-01 | Initial release — HS power, ADC, CAN broadcast |
| 1.1.0 | 2026-01-15 | Added error manager, EEPROM config, shutdown protection |
| 1.2.0 | 2026-02-20 | Added recovery state machine, EEPROM sanitization |
| 1.2.1 | 2026-02-26 | Fixed CAN bit timing (sample point 2.5% → 80%), removed AppInit() dependency, fixed double IRQ handler |