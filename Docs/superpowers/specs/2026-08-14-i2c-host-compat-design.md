# ButtonBoard V5.5: V5.2-compatible I2C host interface

**Date:** 2026-08-14
**Target:** `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG` (hardware-validation firmware)
**Status:** approved in discussion; see review notes at the end

## Purpose

On ButtonBoard V5.2 an ATtiny (`Button-Board_5_2/AttinyInternalKnobCode`) acted
as an I2C slave that a host machine (Jetson) polled for knob state. V5.5
replaced the ATtiny with the STM32G0B1, which reads the encoder and rotary
switch directly and reports over CAN. Existing Jetson-side code still speaks
the ATtiny's I2C protocol. This feature makes the V5.5 firmware answer that
protocol byte-for-byte, so V5.2 host code works against a V5.5 board with no
changes.

## Topology and constraints

> **Revised 2026-08-15.** The V5.5 board now routes the EEPROM to its own
> bus (I2C3 on PA6/PA7). The original design assumed one shared bus and a
> dual-role STM32; that complexity is gone. Earlier wording below is
> superseded by this list.

- **Two independent buses:**
  - **I2C1, PB6/PB7 (`HOST_SCL`/`HOST_SDA`), off-board via connector P1** —
    the host port. The STM32 is a **pure slave** here at 7-bit **0x51**.
    Standard-mode 100 kHz (`Timing = 0x10A077A8`) since 2026-08-19 - for a
    pure slave only SDADEL/SCLDEL matter (the master drives SCL) and the
    100 kHz value gives more data-hold margin on a cable-borne bus.
  - **I2C3, PA6/PA7 (`EEPROM_SCL`/`EEPROM_SDA`)** — the AT24C256 (7-bit
    0x50). The STM32 is the only master. Standard-mode 100 kHz
    (`Timing = 0x10A077A8`), deliberate.
- No dual-role operation, no listen suspend around EEPROM traffic, no
  multi-master arbitration handling: the host cannot see EEPROM traffic
  and vice versa.
- 0x51 matches the V5.2 board's strap wiring (ATtiny base 0x49 + the +8
  strap). Fixed at compile time; no strap pins exist on V5.5.

## Protocol (wire-compatible with the ATtiny)

Command byte written first; reply read back. Replicated exactly, including the
ATtiny's byte-order asymmetry:

| Cmd | Name | Reply | Notes |
|---|---|---|---|
| 0x05 | READ_KNOB_1 | 2 B, **BE** mV | synthesized rotary ladder voltage, see below |
| 0x08 | READ_REFERENCE_VOLTAGE | 2 B, **BE** mV | fixed 3300 |
| 0x09 | READ_ALL | 7 B: knob BE16, ref BE16, encoder BE16, button 1 B | ATtiny default command; also the power-on default here |
| 0x80 | RW_ENCODER | read: 2 B **BE** int16; write: 2 data bytes **LE** int16 | write reuses the CAN `CMD_ENCODER` preset path (`Inputs_SetEncoder`) |
| 0x84 | RW_ENCODER_BUTTON | 1 B: 1 = pressed | |
| 0xFE | READ_VERSION | 2 B: major, minor | reports **2, 0** (ATtiny reported 1, 0) |
| 0x10 | READ_UID | **10 B** | first 10 bytes of the STM32 96-bit UID, matching the ATtiny reply length |

Unknown command: reply bytes are 0xFF (slave must still serve the read
clock; this matches "default: break" on the ATtiny, whose Wire buffer then
sent nothing meaningful).

### Rotary → mV synthesis

V5.2 read the rotary switch as an analog resistor-ladder voltage (10 k between
each pair of adjacent legs, COM at 3.3 V) and reported millivolts. V5.5 reads
the seven position lines (`ROT_SW_0..6`, `rotary_pos` 0..6) digitally and
synthesizes the value the ladder would have produced:

    knob_mV = rotary_pos * 550        /* 0, 550, 1100, ... 3300 */

During the make-before-break transition and during a decode fault, the last
valid position's voltage is held — the same hold-previous rule the digital
decoder itself uses, and approximately what the physical ladder did between
detents.

## Architecture

New module `Core/Src/i2c_host.c` + `Core/Inc/i2c_host.h`, owning only the
slave protocol. Same ISR/main-loop discipline as `can_operation.c`:

- **Snapshot out:** after each `Inputs_Poll`, the main loop calls
  `I2CHost_Publish(const InputState *)`, which refreshes a small volatile
  snapshot (`knob_mV`, `ref_mV`, `encoder`, `button`). The slave ISR serves
  replies purely from this snapshot and never touches input state directly.
- **Commands in:** an encoder write over I2C sets the same
  `encoder_preset` / `encoder_preset_update` flags that CAN `CMD_ENCODER`
  sets, so there is exactly one apply path in `applogic.c`.
- **HAL plumbing:** `OwnAddress1 = 0x51 << 1`; `HAL_I2C_EnableListen_IT` after
  init; `AddrCallback` (direction + prepare reply from `last_command`),
  `SlaveRxCpltCallback` (command byte + optional write payload),
  `SlaveTxCpltCallback`, `ListenCpltCallback` (re-arm listen),
  `ErrorCallback` (recover + re-arm). I2C1 event/error IRQs enabled in NVIC.

## EEPROM (I2C3) — driver migration and status propagation

The EEPROM driver moves from `hi2c1` to a new `hi2c3` handle (I2C3, PA6/PA7,
AF6, 100 kHz). No coexistence logic is needed since the buses are separate.
The migration also fixes what review finding #5 flagged, since the driver is
being touched anyway:

- HAL statuses stop being discarded: `EEPROM_Write_Config` / `Read_Config`
  return `bool`, `service_commands` sets `ERR_EEPROM` on a failed save, and
  the boot-time read treats a bus failure like a bad magic.
- The AT24C256 write-cycle ACK-poll uses HAL's own trials loop
  (`HAL_I2C_IsDeviceReady(&hi2c3, EEPROM_ADDR, 300, 10)`) instead of the
  ten fast NACK probes that returned before the ~5 ms cycle finished.

## Testing

- **Host-side unit tests (vv):** reply framing and the rotary-mV table
  compile host-side; assertions cover the BE/LE asymmetry of 0x80, the 7-byte
  READ_ALL layout, hold-last-value on invalid rotary, and the 10-byte UID
  truncation.
- **Bench:** any I2C master works — the V5.2 ATtiny board reflashed as a test
  master, or the Jetson once the board reaches that bench. Full pass =
  the V5.2 host polling loop (0x09 default) runs unmodified against the
  Nucleo/V5.5 board while CAN broadcasts continue unperturbed, and an EEPROM
  save (`CMD_EEPROM` over CAN) during continuous host polling leaves the host
  port unaffected and reports its result truthfully in `ERR_EEPROM`.

## Out of scope

- Porting to the production firmware tree (this repo validates hardware).
- Emulating V5.2 analog transition voltages between detents (held value
  instead).
- The AT24C256's write-cycle NACK emulation on the bench (needs real board).

## Review notes (user to confirm)

1. Rotary is treated as **7 positions** (0..6, matching `ROT_SW_0..6` and
   6 ladder resistors → taps at 0..3300 mV in 550 mV steps). Jordan described
   it as 6-position — confirm against the real ladder/host thresholds.
2. Version reply 2,0 — flip to 1,0 if any host hard-checks the version.
3. UID = first 10 of 12 STM32 UID bytes — confirm no host expects the
   ATtiny's specific UID structure beyond length.
