# Fabrica bench tool

Flashes and debugs the four Fabrica boards from a Jetson, Raspberry Pi, or any
Ubuntu machine. Bootloaders go over ST-Link V3; applications go over CAN via
OpenBLT BootCommander; telemetry is decoded live from the DBC files.

This directory is the deployment unit. Copy the whole of `Tools/fabrica/` to the
bench machine.

---

## Read this first

**This tool has never touched hardware.** It was written on a Windows box with
no ST-Link, no CAN interface, and no `_curses`. 406 unit tests pass, but they
prove the *logic* — command construction, checksum enforcement, DBC decoding,
error handling. They prove nothing about whether ST-Link enumerates or whether
BootCommander likes our arguments.

So: **start with `doctor`, then a `--dry-run`, then flash one board.** Do not
start by flashing three boards.

Specifically untested, in rough order of risk:

| Risk | Why | First thing to check |
|---|---|---|
| The whole TUI | Windows Python has no `_curses`, so `fabrica/tui.py` has never been executed | Run `./fabrica_cli.py tui`. If it misbehaves, every function is available as a plain CLI subcommand — use those and carry on |
| **st-flash + `.srec`** | st-flash has no S-record parser at all. `doctor` now FAILS if st-flash is the only backend, rather than passing and letting the first flash blow up | Install STM32CubeProgrammer |
| openocd + `.srec` | openocd picks its image parser from the file extension and knows `.s19`, not `.srec`. Given an unrecognised extension it falls back to **raw binary**, which would write the ASCII text of the S-record into flash — silent corruption, not an error. We therefore emit explicit `flash write_image ... s19` rather than the usual `program` helper | Proven on the KincoDrive with openocd 0.12; see the version note below |
| `open_bus("can0")` | A one-line wrapper around `can.Bus`, and the one line no test can reach — the tests use the `virtual` backend | `./fabrica_cli.py monitor --seconds 5` |
| Extended 29-bit CAN | Encoded as `-xid=1` to match `flash_can.sh`. Never exercised by anyone | Nothing to check: all four boards are 11-bit standard |
| `-s=xcp` and `-t1` | `-s=xcp` matches your working script. `-t1` is **not** passed by default, so the argv is byte-for-byte the invocation already proven on the bench | If BootCommander rejects an option, `-s=xcp` is the only non-script flag |

### Layout — the folder is the deployment unit

```
Tools/fabrica/
├── fab                     ./fab, or symlink onto PATH
├── fabrica_cli.py          every operation, as a plain subcommand
├── fabrica/                the package
├── firmware/               one folder per version — YOU name these
│   ├── 2026-08-03-a12db60/     staged by the gate (default name: date+sha)
│   └── bench-test-A/           copied in by hand; picked up automatically
├── tools/                  optional local binaries, preferred over PATH
├── vendor/                 optional vendored python-can + cantools
├── install_openocd012.sh
└── prepare-offline.sh      fills tools/ and vendor/ for this machine
```

Copy the whole folder to a bench and it works:

```bash
scp -r Tools/fabrica user@bench:~/
```

**`firmware/` holds versions, not boards.** Each subfolder is a complete set,
and the name is yours — the tool identifies sets by shape, never by name. Three
shapes are recognised: a folder with `manifest.json`, a folder of `.srec` files,
or a folder of per-board subfolders. Press **`f`** in the TUI to switch between
them; they are listed newest-modified first, then by name.

Point `--firmware` at either a single version or the container. Naming a version
selects exactly that one; naming the container takes the newest.

To stage a build under a name of your choosing:

```bash
python vv/run_gate.py --continue --stage-artifacts --version bench-test-A
```

`tools/` and `vendor/` are `.gitignore`d and optional — see `tools/README.md`
for why the binaries are not committed, and run `./prepare-offline.sh` to fill
both for the machine you are on.

### A board that "looks dead" may just be held by the debugger

A halted STM32 is silent on CAN, draws normal current, and **cannot be flashed
over CAN** — the application that would answer XCP is not executing. It is
indistinguishable from a dead board, and the instinctive response, a power
cycle, destroys the evidence.

The usual cause is an openocd invocation ending in `-c "exit"` instead of
`-c "shutdown"`. `exit` terminates the process without de-initialising the
adapter, so the ST-Link can keep the target in debug state after openocd is
gone. **Always end an openocd script with `shutdown`.**

To recover without power-cycling:

```bash
./fabrica_cli.py release <board>
```

`flash` already resumes the core on every path, including failures. `release`
is for targets left held by something else — an interrupted debug session, a
killed openocd, or a hand-written probe.

### openocd must be ≥ 0.12 to flash a G0B1

Ubuntu 22.04 ships openocd **0.11.0** (jammy/universe, no backport). Its
`stm32l4x` flash driver — the one covering the STM32G0 family — has no entry for
device id `0x467`, so on a **STM32G0B1** it attaches, reads memory and
identifies the core correctly, and only then fails:

```
Warn : Cannot identify target as an STM32G0/G4/L4/L4+/L5/WB/WL family device.
Error: auto_probe failed
```

Everything looks healthy right up to the one operation that matters. This blocks
SWD bootloader flashing on **KincoDrive, PowerStage and LEDDriver**. The F303
knob is unaffected (it goes through `stm32f1x`), and flashing an *application*
over CAN never involves openocd at all.

`doctor` now parses `openocd --version` and warns per MCU family, and `flash`
refuses outright rather than letting openocd halt the core and fail with a
message naming neither the version nor the device. To fix a bench machine:

```bash
./install_openocd012.sh
```

It builds 0.12.0 into `/usr/local` and leaves the distro package in place. A
working 0.12 reports `device idcode = 0x10016467 (STM32G0B/G0Cx)` and
`flash size = 512 KiB, dual-bank`. STM32CubeProgrammer is the other option and
is still the preferred backend where it can be installed.

---

## Install on the bench machine

The tool works out what this host needs and how to install it:

```bash
./fabrica_cli.py setup             # detect host, list what is missing
./fabrica_cli.py setup --install   # run the install commands
```

It detects **Ubuntu**, **Jetson** and **Raspberry Pi** and adapts, because the
part that actually differs is how a CAN interface comes into existence:

| Host | CAN comes from | What `setup` tells you |
|---|---|---|
| Jetson | Native controller (mttcan) | `modprobe can can_raw mttcan`, plus carrier-board pinmux — the CAN pins are usually shared on the 40-pin header |
| Raspberry Pi | MCP2515 SPI HAT | The `dtoverlay=mcp2515-can0` line and which `config.txt` to put it in (`/boot/firmware/` on Bookworm+), and a warning that a wrong `oscillator=` gives a link that comes up but never receives a frame |
| Ubuntu | Usually PEAK USB | In-tree `peak_usb` should create `can0` on plug-in |

Two things `setup` reports but deliberately does **not** install:

- **BootCommander** is built from source, not apt — run `install_openblt.sh`.
- **STM32CubeProgrammer** is a manual download from ST. `stlink-tools` installs easily but **cannot flash this project's images** -
  every one is a `.srec` and st-flash has no S-record parser. `doctor` treats
  st-flash-only as a failure. Install CubeProgrammer.

Python dependencies are also pinned in a requirements file, if you prefer that
to `setup --install`:

```bash
pip3 install -r requirements.txt          # python-can, cantools
pip3 install -r requirements-dev.txt      # + pytest, to run the test suite
```

On **Windows** the TUI additionally needs `windows-curses` (the standard library
ships no `_curses` there). It is already in `requirements.txt` behind a
`sys_platform == "win32"` marker, so it installs only where it is needed and is
ignored on Linux. Every CLI subcommand works on Windows without it - only the
full-screen interface needs it.

Then bring up the bus and verify:

```bash
sudo ./CANBusSetup.sh can0 500000   # from Linux-bash-and-python-script
./fabrica_cli.py doctor
```

`doctor` prints the detected host, and if the CAN checks fail it repeats that
host's specific CAN guidance rather than a generic message.

---

## Monday runbook

Work down this list. Stop at the first failure and fix it before continuing.

**0. Install what this host needs.**

```bash
cd Tools/fabrica
./fabrica_cli.py setup            # review
./fabrica_cli.py setup --install  # then install
```

**1. Prove the environment.**

```bash
./fabrica_cli.py doctor
```

Every row must be `ok`, apart from warnings you have consciously accepted. Each
failure prints the command that fixes it. This checks: ST-Link backend,
BootCommander, python-can, cantools, the CAN interface exists, it is up at
500000 bps, it is not bus-off, and every firmware image matches its manifest
checksum.

**2. Confirm what you are about to flash.**

```bash
./fabrica_cli.py list
```

Check the git SHA. If it says `DIRTY`, the images came from an uncommitted tree
and you cannot reproduce them later.

**3. Review a command without running it.**

```bash
./fabrica_cli.py flash powerstage boot --dry-run
./fabrica_cli.py flash powerstage app  --dry-run
```

Read the argv. This is the moment to catch a wrong tool, a wrong CAN id, or a
wrong file — before anything is written to a board.

**4. See the bus before you touch it.**

```bash
./fabrica_cli.py monitor --board powerstage --seconds 10
```

A board already running its application should broadcast on `0x150`–`0x15A`
every 500 ms. No traffic means wiring, termination, or power — not this tool.

**5. Flash one bootloader, via ST-Link.**

```bash
./fabrica_cli.py flash powerstage boot
```

**6. Flash that board's application, over CAN.**

```bash
./fabrica_cli.py reset powerstage        # trigger the bootloader
./fabrica_cli.py flash powerstage app
```

`reset` sends `id=0x130, data=FF 00`, which the running application answers by
calling `NVIC_SystemReset()`. If the board is already in its bootloader, the
same frame is the XCP CONNECT and is handled there — so this is safe either way.

**7. Confirm it came back.**

```bash
./fabrica_cli.py monitor --board powerstage --seconds 10
```

**7b. Verify the board actually behaves.**

```bash
./fabrica_cli.py verify powerstage --allow-transmit
./fabrica_cli.py verify powerstage --allow-transmit --include-reset
```

Three properties, in order:

| Check | Asserts |
|---|---|
| `unsolicited` | every broadcast the DBC says the board sends arrives **without the host asking**, and more than once so it is demonstrably periodic |
| `causal` | a command visibly changes the telemetry it governs - PowerStage `CMD_FAN` duty must come back in `BCAST_FAN` |
| `reset` | `FF 00` stops the application broadcasts, i.e. the board restarted into its bootloader |

`verify` transmits, so it refuses to run without `--allow-transmit`. The reset
check is separately opt-in because it stops the board. **Your CAN bus is shared
with the Kinco drives**, so run this deliberately rather than by habit.

Note on the causal check: KincoDrive deliberately uses its OC-threshold echo
(`Cmd_OC_Threshold` -> `Bcast_Config_A`) rather than its fan command. Its
`Bcast_Fans` carries fan **tachometer** percent, a measured value - a real fan
takes time to spin up and never reports exactly the commanded duty, so asserting
equality against it would fail on correct hardware.

**7c. Configure it.**

```bash
./fabrica_cli.py config read powerstage
```

Three columns per parameter, and they are three different facts:

| Column | Means |
|---|---|
| `desired` | edited here, not yet sent |
| `live` | what the board is doing right now |
| `stored` | what it will come back to after a power cycle |

They are allowed to disagree. A write moves `live`; only `config save` moves
`stored`. That split is not this tool being fussy - it is how the firmware
works, and a screen showing one number would either hide unsaved work or
report a fault against a board behaving correctly.

```bash
./fabrica_cli.py config write powerstage Fan_Duty=40 --allow-transmit
./fabrica_cli.py config save  powerstage --allow-transmit
```

Every write is verified: the frame goes out, the board's telemetry is read
back, and the value is compared. Nothing acknowledges a CAN command, so the
readback is the only evidence it landed.

Parameters that drive hardware - rail enables, LED outputs, the CAN relay -
need `--allow-actuate` on top of `--allow-transmit`. Two flags because they are
two different risks: one puts frames on a shared bus, the other switches
something.

Note on KincoDrive: its OC and UV thresholds have no live echo. The board sets
them in RAM and only reports them once they are saved, so they read as
`stored` only and stay `-` under `live` until you run `config save`. That is
correct firmware behaviour, not a failed write.

**7d. Make it repeatable.**

```bash
./fabrica_cli.py config dump  powerstage            # board -> profiles/powerstage.json
./fabrica_cli.py config diff  powerstage            # profile vs board
./fabrica_cli.py config apply powerstage --allow-transmit --save
```

`apply` writes only what differs, so re-applying to an already-configured rig
puts nothing on the bus. See `profiles/README.md`.

**8. Only now repeat for the other boards**, and consider the TUI:

```bash
./fabrica_cli.py tui
```

Once one board has been through the whole list, `flash all app` does the rest
in one go. Applications only - a bootloader needs a physical ST-Link probe per
board, so there is no unattended version of that. It runs strictly one board at
a time and stops at the first failure, because a flash that cannot reach its
target leaves BootCommander transmitting XCP CONNECT about 17 times a second;
flashing the next board into that produces a second failure whose real cause is
the first one. `--keep-going` overrides it when you know why.

---

## Commands

```
setup [--install]             detect host, check/install dependencies
sources [--root DIR]          list selectable firmware folders, newest first
doctor                        check tools, CAN link, firmware checksums
list                          boards, CAN ids, image sizes, provenance
flash <board> boot|app        flash via ST-Link (boot) or CAN (app)
flash all app                 every board's application, one at a time
reset <board>                 send the bootloader trigger frame
monitor [--board B]           listen and decode, --seconds N
verify <board>                HIL check of the three behavioural properties
                              (needs --allow-transmit; --include-reset opt-in)
config read <board>           every parameter: desired / live / stored
config write <board> S=V ...  set live values, then verify the readback
config save <board>           commit the live config to EEPROM
config defaults <board>       restore factory defaults
config dump <board>           capture the board into a profile file
config diff <board>           profile vs board
config apply <board>          write what differs, optionally --save
tui                           full-screen interface
```

Global: `--firmware DIR`, `--iface can0`, `--bitrate 500000`, `--no-colour`.
`--dry-run` works on `flash` and `reset`.

Board ids: `kincodrive`, `powerstage`, `leddriver`, `knob`.

### TUI keys

```
j / k    select board          b   flash bootloader (ST-Link)
m        start/stop monitor    a   flash application (CAN)
d        run doctor            r   send bootloader trigger
c        configuration screen  f   choose firmware folder
q        quit

Inside the config screen:

j / k    select parameter      w   write the selected parameter
e        edit its value        W   write every edited parameter
R        re-read the board     s   SAVE the live config to EEPROM
A        allow writing         D   restore factory defaults (press twice)
         parameters that       c / Esc   back to the board list
         drive hardware

Pressing `f` opens a picker listing firmware folders under the parent of the
current one, newest first. Enter selects, Esc cancels.
```

Flashing runs on a worker thread, so the interface keeps redrawing and output
streams into the log pane.

---

## Safety properties

- **Every flash is checksum-verified first.** The image is hashed and compared
  to the manifest immediately before it is sent. A mismatch aborts.
- **The manifest binds an image to a board.** Before it existed, all three G0B1
  bootloaders built to an identically named `openblt_stm32g0b1.srec` differing
  only in the CAN address they answer on, so flashing the wrong one produced a
  board that was alive but silent on the address you expected.
- **Nothing is flashed that the V&V gate has not passed.** `--stage-artifacts`
  only runs on a green gate.
- **`--dry-run` executes nothing** and is asserted in tests to have executed
  nothing, not merely to have returned early.

## Selecting which firmware to flash

`--firmware <dir>` accepts either kind of folder, and the TUI's `f` key picks
one interactively:

| Folder contains | Behaviour |
|---|---|
| `manifest.json` | Staged by the V&V gate. Carries CAN ids, load addresses and **checksums**, so every image is verified before flashing |
| loose `.srec` files | An unverified drop. Board and image kind are inferred from file names, CAN ids come from a config file if present, else built-in defaults |

Folders are listed **newest first by modification time, then by name** - the one
you want is nearly always the one you just built.

A loose folder cannot be checked against any source of truth, so it loads with
`gate=unverified` and `git_dirty=true`. Everything downstream already surfaces
those, and the TUI says so explicitly on selection. Checksums are still computed
so a file that changes *after* selection is caught, but they prove nothing about
provenance.

An optional config file (`fabrica.json`, `config.json` or `boards.json`)
overrides the built-in CAN ids and bitrate per board:

```json
{ "powerstage": { "blt_rx": "0x230", "blt_tx": "0x231", "bitrate": 250000 } }
```

## Regenerating firmware

```bash
python vv/run_gate.py --stage-artifacts
```

Writes `firmware/<board>/*.srec` and `firmware/manifest.json`. The binaries are
deliberately not tracked in git — attach them to a tagged release instead.

## Tests

```bash
python -m pytest Tools/fabrica/tests -q     # 406 tests, no hardware needed
```

## Layout

```
fabrica_cli.py        entry point
fabrica/manifest.py   load + checksum-verify the firmware manifest
fabrica/env.py        tool discovery and the doctor checks
fabrica/stlink.py     ST-Link flashing (CubeProgrammer / openocd / st-flash)
fabrica/canflash.py   BootCommander wrapper, bootloader trigger frame
fabrica/canbus.py     SocketCAN + DBC decode, Monitor
fabrica/host.py       host detection (Ubuntu/Jetson/RPi) and dependency plan
fabrica/tui.py        curses interface
firmware/             staged .srec files + manifest.json (not in git)
```
