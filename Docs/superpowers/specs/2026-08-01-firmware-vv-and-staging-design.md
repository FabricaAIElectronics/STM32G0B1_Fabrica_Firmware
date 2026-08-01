# Firmware V&V Gate and Artifact Staging — Design

**Date:** 2026-08-01
**Status:** Approved
**Scope:** Sub-projects B (V&V gate) and C (artifact staging) of the end-to-end pipeline.

---

## 1. Context

`STM32G0B1_Fabrica_Firmware` holds firmware for four boards, each with a matched OpenBLT
bootloader:

| Board | Application project | Bootloader project | MCU |
|---|---|---|---|
| KincoDrive | `STM32G0B1_Applciationprog/KincoDrive_ControlModule_V5_4` | `STM32G0B1_Bootloader/G0B1_KincoDrive_Boot` | STM32G0B1RET6 |
| PowerStage | `STM32G0B1_Applciationprog/PowerStage` | `STM32G0B1_Bootloader/G0B1_PowerStage_Boot` | STM32G0B1RET6 |
| LEDDriver | `STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG` | `STM32G0B1_Bootloader/G0B1_LEDDriver_Boot` | STM32G0B1RET6 |
| Knob | `STM32F303_Applciationprog/Fabrica_STM32F3_Prog` | `STM32F303_Bootloader/Fabrica_STM32F3RE_Boot` | STM32F303RET6 |

`STM32G0B1_Applciationprog/ADC_STM32G0B1` is a CubeMX scaffold with an empty `main()`. It is
**out of scope** for every stage below.

This firmware is for hardware verification and validation; production firmware for these
products lives outside this repo. The gate is a **one-time pre-release check** run manually,
not continuous integration. No GitHub Actions, no self-hosted runner, no commit hooks.

### Decisions taken during design

| Decision | Choice | Rationale |
|---|---|---|
| TUI location | `Tools/fabrica/` inside this repo | Version-locks firmware to the tool that flashes it |
| Test depth | Unit tests **and** CAN protocol conformance | Would have caught review findings 3, 8, 10, 11 |
| Protocol source of truth | The `.dbc` files | `can_decoder.py` already parses them with `cantools`; PCAN-Explorer and Vector consume them directly |
| Gate host | Windows | CubeIDE 1.18.0 is installed and its headless build is already proven working |
| Host compiler | Assumed present; gate fails loudly if absent | Unit tests are the highest-value stage; silently skipping them would make a green run meaningless |

---

## 2. Layout

```
vv/                             # sub-project B. Dev-time only, never shipped.
  run_gate.py                   #   single entry point
  baseline.txt                  #   accepted pre-existing compiler warnings
  boards.py                     #   the one place board metadata is declared
  checks/
    preflight.py
    static.py
    build.py
    size.py
    conformance.py
  unit/
    Makefile                    #   host build, native gcc
    fakes/                      #   fake HAL headers + stubs
    test_*.c
  tests/                        #   pytest tests OF THE GATE ITSELF
    fixtures/

Tools/fabrica/                  # sub-project C. This directory is the deployment unit.
  firmware/
    manifest.json
    kincodrive/  powerstage/  leddriver/  knob/    # <board>/*.srec
```

`Tools/fabrica/` is copied whole to the Jetson/RPi/Ubuntu machine. The TUI (sub-project D)
will be added beside `firmware/`, so the directory is self-contained: the tool plus the
images it flashes.

`vv/` and `Tools/fabrica/firmware/` are both git-tracked. Build output under project
`Debug/` directories remains untracked per the root `.gitignore`.

---

## 3. The gate

Entry point:

```bash
python vv/run_gate.py [--continue] [--stage <name>] [--json <path>] [--update-baseline]
```

Default behaviour is fail-fast: the first hard failure stops the run and exits non-zero.
`--continue` runs every stage and reports all failures together. `--stage` runs one stage in
isolation for debugging. `--json` writes the structured result for later inspection.

Every stage returns the same structure:

```python
StageResult(name: str, status: "pass"|"fail"|"warn", detail: str, items: list[dict])
```

The runner prints a summary table and exits `0` only when no stage failed.

### Stage 0 — preflight

Asserts the toolchain is present and reports versions:

- `arm-none-eabi-gcc` on `PATH`
- a native C compiler (`gcc`) on `PATH`
- STM32CubeIDE at `C:/ST/STM32CubeIDE_1.18.0/STM32CubeIDE/stm32cubeidec.exe`, overridable
  via the `CUBEIDE` environment variable
- Python `cantools`

Missing tools produce a hard failure that names each one and how to install it. This stage
exists because the failure modes downstream are otherwise cryptic.

### Stage 1 — static analysis

Compiles each project's **own** sources with `-fsyntax-only` and:

```
-Wall -Wextra -Wshadow -Wundef -Wpointer-arith -Wstrict-prototypes
-Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wnull-dereference
-Wjump-misses-init -Wswitch-default -Wsign-compare
```

Excluded from the scan: `Drivers/` (ST HAL and CMSIS), `ThirdParty/openblt/` (vendored),
and the generated files `syscalls.c`, `sysmem.c`, `system_stm32*.c`.

`App/flash_layout.c` is also excluded. It is `#include`-d into OpenBLT's `flash.c` rather
than compiled standalone, so scanning it in isolation produces a spurious `unknown type
name 'tFlashSector'` error.

**Baselining.** The tree currently carries about 82 warnings per bootloader, dominated by
`-Wstrict-prototypes` in the OpenBLT-derived files under each bootloader's own `App/`
directory. Those files live inside the projects, so the `ThirdParty/` exclusion above does
not cover them. A gate that is red on day one teaches people to ignore it. So:

- `vv/baseline.txt` holds one normalised warning per line: `<relpath>:<line>:<col>: <text>`.
- The stage fails only on warnings **absent** from the baseline.
- Warnings in the baseline that no longer occur are reported as `warn`, prompting baseline
  cleanup, but do not fail the gate.
- `python vv/run_gate.py --stage static --update-baseline` rewrites the file.

Paths in `baseline.txt` are repo-relative with forward slashes so the file is stable across
machines.

### Stage 2 — host unit tests

`vv/unit/Makefile` compiles the pure-logic modules with the **native** compiler against a
fake HAL, links a small assertion harness, and runs it. No CMake — `make` is already
available and CMake is not.

Modules under test, and what is asserted:

| Module | Assertions |
|---|---|
| `PowerStage/Core/Src/battery.c` | SOC curve endpoints and monotonicity; cutoff 19.6 V, full 25.2 V, 6 cells; internal-resistance compensation |
| `KincoDrive/Core/Src/power_monitor.c` | OC trips at threshold and not below; UV asserts and clears with the documented 500 mV hysteresis; error-mask bit positions |
| `KincoDrive/Core/Src/thermistor.c` | Beta equation against known resistance/temperature pairs; open-sensor detection below the noise floor |
| CAN pack/unpack, all four boards | Every command and broadcast: id, DLC, byte order, field offsets |

The fake HAL lives in `vv/unit/fakes/`. It provides the handful of symbols these modules
reference (`HAL_GetTick`, `ADC_VAL`, GPIO read/write, FDCAN transmit) with test-controllable
state. Modules that touch hardware directly and have no separable logic — `ssd1306.c`,
`Fan_PWM.c`, `hs_switch.c` — are **not** unit tested; that is deliberate, and their coverage
comes from HIL later.

The CAN pack/unpack tests are the load-bearing part: they are also the executable
specification that stage 5 checks the DBC against, and that the TUI's decoder must match.

### Stage 3 — firmware build

Headless CubeIDE, all eight projects, `Debug` configuration only. `Release` is unconfigured
in every project and is not built.

```
stm32cubeidec.exe --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data <scratch-workspace> -import <project-dir> -cleanBuild "<Name>/Debug"
```

Each project builds in its own scratch workspace under the system temp directory, which is
removed first so every run is clean. Note that the three G0B1 bootloaders now have unique
project names, so a shared workspace would also work; per-project workspaces are kept because
they make a single failing project easy to isolate.

The stage fails on any compiler error, and on any warning not in the baseline.

### Stage 4 — size gate

Parses `arm-none-eabi-size` output for each artifact and checks it against the region it
must fit:

| Artifact | Region | Limit |
|---|---|---|
| G0B1 bootloaders | `0x08000000`, reserved | 12 KB |
| F303 bootloader | `0x08000000`, reserved | 14 KB |
| G0B1 applications | `0x08003000` → end of flash | 512 KB − 12 KB |
| F303 application | `0x08003800` → end of flash | 512 KB − 14 KB |

`flash = text + data`, `ram = data + bss`. Over the limit is a hard failure. Above 80 % of
the limit is a `warn`, because a bootloader that outgrows its reservation silently corrupts
the application it is supposed to load.

The F303 bootloader's linker script declares `LENGTH = 512K` rather than its true 14 KB
reservation, so the linker will not catch an overflow itself. This stage is currently the
only thing that would. Correcting that linker script is tracked separately as review
finding 9.

### Stage 5 — protocol conformance

Three descriptions of the CAN protocol must agree:

1. the `.dbc` files (authoritative),
2. the firmware's `#define`s for message IDs,
3. the table in `Docs/CAN_Bus.md`,

plus a fourth, indirect one: the byte layouts asserted by the stage 2 pack/unpack tests.

Statically proving that a C function consumes N bytes would require parsing C control flow.
That is fragile and would give false confidence, so it is explicitly **not** attempted.
Instead the unit tests carry the layout claim and this stage checks the three descriptions
agree with it and with each other.

Checks performed:

- Every message in a board's DBC has a matching `#define` in that board's headers at the
  same numeric id.
- Every command/broadcast `#define` in a board's headers appears in its DBC. This is what
  catches `CMD_OC_RESET` (0x148), implemented but absent from both the DBC and the docs.
- Every DBC message appears in the `Docs/CAN_Bus.md` table with a matching id and DLC. This
  is what catches `BCAST_OC_CFG_B` (0x158), documented but never implemented.
- Each board's bootloader RX/TX ids match `BOOT_COM_CAN_RX_MSG_ID` / `BOOT_COM_CAN_TX_MSG_ID`
  in that bootloader's `App/blt_conf.h`.
- Message ids fall inside the board's allocated sub-block per `Docs/CAN_Bus.md` §2.
- Byte order declared in the DBC matches the order asserted by the stage 2 tests.

**Known exception.** The knob board uses ids `0x661`–`0x667` and `0x7E1`, which sit in
CANopen SDO space and do not follow the address plan. This is known, is owned by another
team, and must **not** fail the gate. `vv/boards.py` marks the knob
`address_plan_exempt = True`, and the stage emits a `warn` recording the exemption rather
than a failure.

Firmware `#define`s are extracted by regex over the relevant headers, not by compiling them.
The set of headers to scan is declared per board in `vv/boards.py`.

---

## 4. Artifact staging

Staging runs only after a fully green gate. A binary that has not passed can never reach
`Tools/`.

For each board, `.srec` files for the bootloader and application are copied to
`Tools/fabrica/firmware/<board>/`, and `manifest.json` is regenerated.

Every `file` value in the manifest is relative to `Tools/fabrica/firmware/`, so the TUI
resolves paths against the manifest's own directory and the whole folder stays relocatable.
Artifact filenames come from each project's `artifactName`, which is why they do not all
match the board name — the KincoDrive application, for example, builds as
`Actuation_IO_Distribution_Board_Embedded.srec`. The manifest is the mapping.

```json
{
  "schema": 1,
  "generated": "2026-08-01T12:00:00Z",
  "git_sha": "ea79ea6...",
  "git_dirty": false,
  "gate": "pass",
  "boards": [
    {
      "id": "powerstage",
      "name": "PowerStage",
      "mcu": "STM32G0B1RET6",
      "boot": {
        "file": "powerstage/G0B1_PowerStage_Boot.srec",
        "sha256": "...", "flash_bytes": 10136, "load_addr": "0x08000000"
      },
      "app": {
        "file": "powerstage/PowerStage.srec",
        "sha256": "...", "flash_bytes": 54168, "load_addr": "0x08003000"
      },
      "can": {
        "blt_rx": "0x130", "blt_tx": "0x131",
        "bitrate": 500000, "extended": false
      },
      "dbc": "PowerStage.dbc",
      "address_plan_exempt": false
    }
  ]
}
```

`git_dirty` records whether the working tree had uncommitted changes at staging time. A
dirty manifest is still written — the gate is run during development — but the TUI will
surface it, because "which source produced this binary" is unanswerable for a dirty tree.

The manifest is what makes staging safe. Before this, all three G0B1 bootloaders emitted a
file named `openblt_stm32g0b1.srec`, differing only in the CAN address they answer on;
flashing the wrong one produced a board that was alive but silent on the expected address.
With the manifest the TUI can verify a checksum and refuse to flash an image whose declared
board does not match the target.

---

## 5. Testing the gate itself

`vv/tests/` holds pytest tests of the gate:

- a fixture source file containing a known defect that stage 1 must flag;
- a fixture whose warning is present in a fixture baseline, asserting it is *not* flagged —
  this guards the baseline logic, which is the easiest part to get silently wrong;
- a fixture DBC that disagrees with a fixture header, asserting stage 5 fails;
- a manifest schema test — required keys present, checksums matching file contents;
- an assertion that the knob's address-plan exemption produces `warn`, not `fail`.

Stages 2 to 4 are not themselves unit tested; they invoke real compilers and their output is
the test.

---

## 6. Out of scope

The TUI, ST-Link flashing, CAN flashing, and anything requiring hardware are sub-projects D
and E. When B and C are complete, `Tools/fabrica/` contains only `firmware/`.

Also out of scope: auditing OpenBLT itself, reducing the warning baseline, and review
findings 5, 9, 10 and 11. Findings 10 and 11 will be *detected* by stage 5 but fixing them
is separate work.

---

## 7. Success criteria

1. `python vv/run_gate.py` runs all six stages and exits `0` on the current tree.
2. Introducing the `~gpio_flag_check()` defect again causes stage 1 to fail.
3. Introducing a DBC/firmware id mismatch causes stage 5 to fail.
4. Padding a bootloader past its reserved region causes stage 4 to fail.
5. A green run produces `Tools/fabrica/firmware/manifest.json` listing all four boards with
   checksums that match the staged files.
6. `pytest vv/tests` passes.
