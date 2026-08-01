# Firmware V&V Gate

Pre-release verification for the Fabrica firmware. Run it manually before a
release. This is **not** CI — there are no hooks, no GitHub Actions, no runner.

## Setting up on a new machine

Clone, then just run it:

    python vv/run_gate.py --continue

Nothing is pinned to one machine. Stages whose tools are missing report **SKIP**
rather than FAIL, so a fresh checkout gives you a useful partial result and a
list of what to install, instead of a wall of red that looks like broken
firmware. A typical first run on a bare machine:

    preflight    WARN   1 present, missing: arm-none-eabi-gcc, gcc, make, stm32cubeidec
    static       SKIP   arm-none-eabi-gcc not on PATH - nothing compiled
    unit         SKIP   no host toolchain (make, gcc not on PATH)
    build        SKIP   STM32CubeIDE not found - nothing was built
    size         SKIP   STM32CubeIDE not found - no artifacts to measure
    conformance  WARN   1 unchecked area(s)

    VERDICT: PASS with 4 stage(s) SKIPPED

Every skip carries the command that fixes it. Install, re-run, watch the skips
turn into passes.

**Before an actual release, use `--strict`**, which turns every skip into a
failure — "we could not check it" must not pass a release gate. `--stage-artifacts`
already refuses to stage from a partial run for the same reason.

### What is and is not portable

| Portable | How |
|---|---|
| STM32CubeIDE | Discovered by glob across Windows `C:\ST\`, Program Files, Linux `/opt/st/`, and macOS. Any version matches — nothing is pinned to 1.18.0. Override with `CUBEIDE=<path>` |
| OpenBLT | Vendored in `ThirdParty/openblt`; no external clone needed |
| Project files | `.cproject`/`.project` reference OpenBLT through `${ProjDirPath}` and `PARENT-2-PROJECT_LOC`, both repo-relative |
| The warning baseline | Records which compiler produced it. On a different `arm-none-eabi-gcc` the gate reports "re-baseline needed" as a WARN instead of reporting phantom regressions |

The genuinely machine-specific parts — STM32CubeIDE and the toolchains — cannot
be vendored. They are **detected and reported**, never assumed.

An explicit `CUBEIDE=` pointing at a file that does not exist is treated as an
error rather than falling back to discovery, so a typo cannot silently build
with a different CubeIDE than you asked for.

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
| size | every artifact fits its flash region (coarse - see below) |
| memmap | the .srec files do not overlap and stay inside their regions |
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

## Current status

The gate exits 0. Two warnings stand, both understood:

- **Size:** reports the three G0B1 bootloaders at 82.8% of their 12 KB
  reservation. **That figure is pessimistic** - see below. The memmap stage
  measures the artifacts directly and puts them at 72.6%.
- **Conformance:** the knob board is unchecked (no DBC, absent from the docs).

### Findings the first real run produced, and how they were resolved

| ID | Finding | Resolution |
|---|---|---|
| `0x141` Cmd_HS | Docs said DLC 5; firmware reads byte[0] as a bitmask and the DBC says DLC 1 | Docs corrected to 1; `layouts.h` corrected to match |
| `0x148` Cmd_OC_Reset | In firmware and DBC, undocumented | Added to `Docs/CAN_Bus.md` |
| `0x158` Bcast_OC_Cfg_B | Documented but never implemented; would have carried the SBC rail's OC threshold | Deleted. `RAIL_SBC` has no MCU-driven EN line and no software OC, and all four protected rails fit in `0x157` |
| `0x154` Bcast_EEPROM | Reported a byte-order mismatch | Not a firmware defect — the message's only multi-byte signal is `Cfg_Reserved`, padding. Reserved signals no longer set a message's byte order |

Worth noting for anyone reading a future failure: on first run the stage
reported 28 mismatches, of which 23 were its own parser bug and one more was
this reserved-signal artifact. Investigate the stage before trusting a large
finding count.

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


## Two ways of measuring flash, and which to believe

`size` and `memmap` disagree about the bootloaders, and memmap is right.

`arm-none-eabi-size` in berkeley format counts *read-only allocated* sections as
text. The OpenBLT-derived linker script marks `.bss` as `ALLOC, READONLY` and
`.data` as `READONLY, CODE`, so `size` folds `.bss` (1264 B) into text and
reports `data=0`:

    size:  text 10180  data 0  bss 1536      -> 82.8% of the 12 KB reservation
    srec:  0x08000000-0x080022D7, 8916 B     -> 72.6%

The S-records contain exactly the bytes that reach the device, so the memmap
figure is the one to plan against. `size` also *under*-reports RAM for the same
reason: real RAM use is `.data` 72 + `.bss` 1264 + heap/stack 1536 = 2872 B, not
the 1536 it prints.

The size stage is kept as a coarse early warning that needs nothing but the
build. memmap needs only the `.srec` files, so it runs even on machines where
the build stage has to skip.

Fixing the linker-script section flags would make both agree; that is upstream
OpenBLT territory and has not been attempted.
