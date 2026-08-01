# Firmware V&V Gate

Pre-release verification for the Fabrica firmware. Run it manually before a
release. This is **not** CI — there are no hooks, no GitHub Actions, no runner.

## Prerequisites

- STM32CubeIDE 1.18.0, or set `CUBEIDE=<path to stm32cubeidec.exe>`
- GNU Arm Embedded Toolchain on `PATH`
- A native C compiler and `make` — MSYS2: `pacman -S mingw-w64-x86_64-gcc make`
- `pip install cantools pytest`

On Windows, put `C:\msys64\mingw64\bin` and `C:\msys64\usr\bin` on `PATH`
**ahead of** `C:\Program Files (x86)\GnuWin32\bin`. GnuWin32's `make` 3.81 hands
recipes to `cmd.exe`, which breaks `mkdir -p` and `rm -rf`; MSYS2's `make` 4.4.1
does not.

Stage 0 checks all of this and names anything missing.

## Usage

    python vv/run_gate.py                    # every stage, stop at first failure
    python vv/run_gate.py --continue         # run everything, report all failures
    python vv/run_gate.py --stage static     # one stage only
    python vv/run_gate.py --json out.json    # machine-readable results
    python vv/run_gate.py --stage-artifacts  # on success, stage to Tools/fabrica/
    python vv/run_gate.py --update-baseline  # accept the current warning set

`python -m vv.run_gate ...` works identically.

A full run takes roughly 5-10 minutes; the build stage does eight headless
CubeIDE builds.

## Stages

| Stage | Checks |
|---|---|
| preflight | required tools are installed |
| static | no compiler warnings beyond `vv/baseline.txt` |
| unit | host-compiled logic and CAN layout assertions |
| build | all 8 projects build headless, Debug config |
| size | every artifact fits its flash region |
| conformance | DBC, firmware `#define`s, `Docs/CAN_Bus.md` and the unit-test layouts agree |

Run the gate's own tests with `python -m pytest vv/tests -q`, and the C
assertions with `make -C vv/unit clean all`.

## What "green" does and does not mean

**The static stage is baselined.** `vv/baseline.txt` records 37 pre-existing
warnings, mostly `-Wstrict-prototypes` in the OpenBLT-derived files under each
bootloader's `App/`. Green means *no new warnings*, not *no warnings*. Shrinking
the baseline is optional cleanup; regenerate it with `--update-baseline`.

**The knob board is only partly covered.** It has no DBC and does not appear in
`Docs/CAN_Bus.md`, so conformance reports it as unchecked rather than failing.
Its CAN ids sit in CANopen SDO space and are owned by another team, so the
address-plan check skips it by design (`address_plan_exempt` in `vv/boards.py`).

**Some modules are not unit tested.** `battery.c` is compiled and tested for
real. `thermistor.c` has its Beta equation restated because it reaches ADC/DMA
state through `adc_driver.h`. `power_monitor.c`, `ssd1306.c`, `Fan_PWM.c` and
`hs_switch.c` have no host tests at all — they need hardware.

## Known findings

The conformance stage currently fails on five real defects, all PowerStage:

| ID | Finding |
|---|---|
| `0x141` Cmd_HS | DBC says DLC 1; firmware and docs say 5 — the DBC is wrong |
| `0x154` Bcast_EEPROM | DBC says little-endian; the layout tests say big |
| `0x148` Cmd_OC_Reset | in the DBC, absent from `Docs/CAN_Bus.md` |
| `0x158` Bcast_OC_Cfg_B | in the docs, absent from the DBC and from firmware |

`0x141` matters most: anything generated from that DBC, including
`can_decoder.py`, will decode a 5-byte command as 1 byte.

The size stage warns that the three G0B1 bootloaders sit at ~82% of their 12 KB
reservation.

## Artifact staging

`--stage-artifacts` runs only after a fully green gate, so an unverified binary
cannot reach `Tools/`. It copies each `.srec` to
`Tools/fabrica/firmware/<board>/` and writes `manifest.json` recording, per
board: MCU, bootloader and application file names, sha256, flash bytes, load
address, and the CAN bootloader RX/TX pair.

That manifest is the point. Before it, all three G0B1 bootloaders emitted an
identically named `openblt_stm32g0b1.srec` differing only in the CAN address
they answer on, and flashing the wrong one produced a board that was alive but
silent on the address you expected. The TUI reads the manifest, verifies the
checksum, and can refuse an image whose declared board does not match the
target.

`git_dirty` in the manifest records whether the working tree had uncommitted
changes at staging time. A dirty manifest is still written — the gate gets run
during development — but the TUI should surface it, because "which source
produced this binary" has no answer for a dirty tree.
