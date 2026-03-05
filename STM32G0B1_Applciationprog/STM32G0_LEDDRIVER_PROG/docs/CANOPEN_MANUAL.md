# LED Driver CANopen Communication Manual

## Overview

The LED Driver communicates exclusively via **CANopen** protocol on a standard CAN 2.0 bus.
All device parameters are accessed through **SDO** (Service Data Object) reads and writes.
Device state is monitored via **NMT** (Network Management) and **Heartbeat** messages.

- **Node ID**: `0x10`
- **Baud rate**: 500 kbps (configured by OpenBLT bootloader via `blt_conf.h` BOOT_COM_CAN_BAUDRATE)
- **CAN frame type**: Standard 11-bit IDs, classic CAN (8-byte data)
- **Bootloader RX ID**: `0x010` (matches node ID, used for OpenBLT raw reset frame)

---

## 1. Message Overview

| CAN ID  | Direction    | Protocol   | Description                  |
|---------|--------------|------------|------------------------------|
| 0x000   | Master -> Node | NMT      | Network management control   |
| 0x610   | Master -> Node | SDO Req  | SDO request (read/write)     |
| 0x590   | Node -> Master | SDO Resp | SDO response                 |
| 0x710   | Node -> Master | Heartbeat| NMT state heartbeat          |
| 0x010   | Master -> Node | Raw      | Bootloader reset trigger     |

> CAN IDs are computed as: `base_COB_ID + node_id`. With node ID = 0x10:
> SDO Request = 0x600 + 0x10 = **0x610**, SDO Response = 0x580 + 0x10 = **0x590**, Heartbeat = 0x700 + 0x10 = **0x710**

---

## 2. NMT (Network Management)

### 2.1 NMT Control (Master -> Node)

Send on **CAN ID 0x000**, 2 bytes:

| Byte | Description |
|------|-------------|
| D0   | NMT command |
| D1   | Node ID (0x10, or 0x00 for all nodes) |

**NMT Commands:**

| Command | Value | Description |
|---------|-------|-------------|
| Start (Operational) | `0x01` | Enter operational mode |
| Stop                | `0x02` | Enter stopped mode |
| Pre-Operational     | `0x80` | Enter pre-operational mode |
| Reset Node          | `0x81` | Reset the application |
| Reset Communication | `0x82` | Reset the communication layer |

**Example: Start node 0x10 (enter operational)**
```
CAN ID: 0x000
Data:   [0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
         ^cmd  ^node_id
```

**Example: Reset node 0x10**
```
CAN ID: 0x000
Data:   [0x81, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
```

### 2.2 Heartbeat (Node -> Master)

Sent periodically by the device on **CAN ID 0x710**, 1 byte:

| Byte | Description |
|------|-------------|
| D0   | NMT state   |

**Heartbeat State Values:**

| State            | Value  |
|------------------|--------|
| Boot-up          | `0x00` |
| Stopped          | `0x04` |
| Operational      | `0x05` |
| Pre-Operational  | `0x7F` |

The heartbeat is sent every **200 ms** while the device is running.

---

## 3. SDO (Service Data Object)

All parameter access uses expedited SDO transfer (data fits in a single 8-byte frame).

### 3.1 SDO Read Request (Master -> Node)

Send on **CAN ID 0x610**:

| Byte | Description |
|------|-------------|
| D0   | Command: `0x40` (read request) |
| D1   | Index low byte |
| D2   | Index high byte |
| D3   | Subindex |
| D4-D7 | Reserved (0x00) |

### 3.2 SDO Read Response (Node -> Master)

Received on **CAN ID 0x590**:

| Byte | Description |
|------|-------------|
| D0   | Command: `0x4F` (1 byte), `0x4B` (2 bytes), `0x43` (4 bytes) |
| D1   | Index low byte |
| D2   | Index high byte |
| D3   | Subindex |
| D4-D7 | Data (little-endian) |

### 3.3 SDO Write Request (Master -> Node)

Send on **CAN ID 0x610**:

| Byte | Description |
|------|-------------|
| D0   | Command: `0x2F` (1 byte), `0x2B` (2 bytes), `0x23` (4 bytes) |
| D1   | Index low byte |
| D2   | Index high byte |
| D3   | Subindex |
| D4-D7 | Data (little-endian) |

### 3.4 SDO Write Acknowledge (Node -> Master)

Received on **CAN ID 0x590**:

| Byte | Description |
|------|-------------|
| D0   | `0x60` (write acknowledge) |
| D1   | Index low byte |
| D2   | Index high byte |
| D3   | Subindex |
| D4-D7 | 0x00 |

### 3.5 SDO Error Response (Node -> Master)

Received on **CAN ID 0x590**:

| Byte | Description |
|------|-------------|
| D0   | `0x80` (abort/error) |
| D1   | Index low byte |
| D2   | Index high byte |
| D3   | Subindex |
| D4-D7 | Abort code (little-endian, see table below) |

**SDO Abort Codes:**

| Code        | Hex Value     | Description |
|-------------|---------------|-------------|
| Bad Command | `0x05040001`  | Invalid or unsupported SDO command |
| No Read     | `0x06010001`  | Read access not permitted |
| No Write    | `0x06010002`  | Write access not permitted |
| No Object   | `0x06020000`  | Object does not exist in dictionary |
| No Subindex | `0x06090011`  | Subindex does not exist |
| Invalid Val | `0x06090030`  | Invalid value for parameter |

---

## 4. Object Dictionary

### 4.1 Index 0x6000 — PWM Control

| Subindex | Type | Access | Description | Range |
|----------|------|--------|-------------|-------|
| 0        | u8   | RO     | Number of subindices (3) | - |
| 1        | u16  | RW     | PWM Channel 0 duty cycle | 0 - 100 (%) |
| 2        | u16  | RW     | PWM Channel 1 duty cycle | 0 - 100 (%) |
| 3        | u16  | RW     | PWM Channel 2 duty cycle | 0 - 100 (%) |

Values above 100 are clamped to 100.

### 4.2 Index 0x6001 — Undervoltage Thresholds

| Subindex | Type | Access | Description | Range |
|----------|------|--------|-------------|-------|
| 0        | u8   | RO     | Number of subindices (2) | - |
| 1        | u16  | RW     | 24V undervoltage threshold (ADC units) | 0 = disabled |
| 2        | u16  | RW     | 17.5V undervoltage threshold (ADC units) | 0 = disabled |

Setting a threshold to 0 disables the undervoltage check for that rail.

### 4.3 Index 0x6002 — Live ADC Readings

| Subindex | Type | Access | Description |
|----------|------|--------|-------------|
| 0        | u8   | RO     | Number of subindices (2) |
| 1        | u16  | RO     | 24V rail ADC reading (12-bit) |
| 2        | u16  | RO     | 17.5V rail ADC reading (12-bit) |

### 4.4 Index 0x6003 — Device Status

| Subindex | Type | Access | Description |
|----------|------|--------|-------------|
| 0        | u8   | RO     | Number of subindices (2) |
| 1        | u8   | RO     | Device state |
| 2        | u8   | RO     | Error code bitmask |

**Device States:**

| Value | State |
|-------|-------|
| 0     | INIT |
| 1     | LOAD_CONFIG |
| 2     | RUNNING |
| 3     | ERROR |
| 4     | RECOVERY |

**Error Codes (bitmask):**

| Bit | Description |
|-----|-------------|
| 0   | 24V undervoltage |
| 1   | 17.5V undervoltage |

### 4.5 Index 0x6004 — EEPROM Command

| Subindex | Type | Access | Description |
|----------|------|--------|-------------|
| 1        | u8   | WO     | EEPROM command byte |

**Command Bits:**

| Bit | Action |
|-----|--------|
| 0   | Write current config to EEPROM |
| 1   | Reset to factory defaults |

Write `0x01` to save, `0x02` to reset defaults, `0x03` to reset defaults and save.

### 4.6 Index 0x6005 — Bootloader Trigger

| Subindex | Type | Access | Description |
|----------|------|--------|-------------|
| 1        | u8   | WO     | Write `0xFF` to trigger system reset into bootloader |

---

## 5. Command Examples (Hex)

All examples assume **Node ID = 0x10**.

### Set PWM Channel 0 to 75%

**Request (Master -> 0x610):**
```
[0x2B, 0x00, 0x60, 0x01, 0x4B, 0x00, 0x00, 0x00]
 ^cmd  ^idx_lo ^idx_hi ^sub ^data_lo ^data_hi
  2B = write 2 bytes    6000  sub 1    75 = 0x004B
```

**Response (Node -> 0x590):**
```
[0x60, 0x00, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00]
 ^ack  ^idx         ^sub
```

### Read PWM Channel 0

**Request (Master -> 0x610):**
```
[0x40, 0x00, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00]
 ^read ^idx_lo ^idx_hi ^sub
```

**Response (Node -> 0x590):**
```
[0x4B, 0x00, 0x60, 0x01, 0x4B, 0x00, 0x00, 0x00]
 ^4B=2byte ^idx    ^sub  ^75(0x004B)
```

### Read 24V ADC Value

**Request (Master -> 0x610):**
```
[0x40, 0x02, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00]
       ^idx=0x6002 ^sub=1
```

**Response (Node -> 0x590):**
```
[0x4B, 0x02, 0x60, 0x01, 0xA0, 0x0F, 0x00, 0x00]
                         ^ADC value = 0x0FA0 = 4000
```

### Read Device State

**Request (Master -> 0x610):**
```
[0x40, 0x03, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00]
       ^idx=0x6003 ^sub=1
```

**Response (Node -> 0x590):**
```
[0x4F, 0x03, 0x60, 0x01, 0x02, 0x00, 0x00, 0x00]
 ^4F=1byte               ^state=2 (RUNNING)
```

### Set 24V Undervoltage Threshold to 500

**Request (Master -> 0x610):**
```
[0x2B, 0x01, 0x60, 0x01, 0xF4, 0x01, 0x00, 0x00]
 ^2B=write2  ^idx=0x6001 ^sub=1 ^500=0x01F4
```

### Save Config to EEPROM

**Request (Master -> 0x610):**
```
[0x2F, 0x04, 0x60, 0x01, 0x01, 0x00, 0x00, 0x00]
 ^2F=write1  ^idx=0x6004 ^sub=1 ^cmd=0x01(write)
```

### Reset to Factory Defaults

**Request (Master -> 0x610):**
```
[0x2F, 0x04, 0x60, 0x01, 0x02, 0x00, 0x00, 0x00]
                                ^cmd=0x02(reset)
```

### Trigger Bootloader Reset (SDO method)

**Request (Master -> 0x610):**
```
[0x2F, 0x05, 0x60, 0x01, 0xFF, 0x00, 0x00, 0x00]
 ^write1byte ^idx=0x6005 ^sub=1 ^0xFF=reset
```

Device will perform `NVIC_SystemReset()` immediately. No SDO response is sent.

### Trigger Bootloader Reset (Raw frame — OpenBLT compatible)

Any standard-ID frame with `data[0] = 0xFF` and `DLC = 2` triggers an immediate system
reset into the bootloader. This is checked before CANopen processing, so no SDO wrapping
is needed.

**Request (Master -> 0x010):**
```
CAN ID: 0x010
DLC:    2
Data:   [0xFF, 0x00]
```

Device resets immediately. No response is sent.

### Start Node (NMT Operational)

**NMT command (Master -> 0x000):**
```
[0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
 ^start ^node_id=0x10
```

---

## 6. Boot-up Sequence

1. Device powers on / resets
2. FDCAN peripheral initialized, filters configured
3. CANopen bridge initializes object dictionary
4. NMT transitions to **Pre-Operational** state
5. Device sends **Heartbeat** with status `0x7F` (pre-operational) on CAN ID `0x710`
6. Device begins sending heartbeat every 200 ms
7. Master sends **NMT Start** (`0x01, 0x10`) to enter operational mode
8. Device heartbeat changes to `0x05` (operational)
9. SDO read/write access is available in both pre-operational and operational states

---

## 7. FDCAN Hardware Configuration

| Parameter | Value |
|-----------|-------|
| FIFO0     | SDO, PDO, and other standard-ID frames |
| FIFO1     | NMT control frames only (CAN ID 0x000) |
| Filter 0  | Exact match ID=0x000 -> FIFO1 |
| Filter 1  | Accept all standard IDs -> FIFO0 |
| TX        | Standard 11-bit IDs, classic CAN |
