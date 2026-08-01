# Fabrica bench tool

Flashes and debugs the four Fabrica boards from a Jetson, Raspberry Pi, or any
Ubuntu machine. Bootloaders go over ST-Link V3; applications go over CAN via
OpenBLT BootCommander; telemetry is decoded live from the DBC files.

This directory is the deployment unit. Copy the whole of `Tools/fabrica/` to the
bench machine.

---

## Read this first

**This tool has never touched hardware.** It was written on a Windows box with
no ST-Link, no CAN interface, and no `_curses`. 177 unit tests pass, but they
prove the *logic* — command construction, checksum enforcement, DBC decoding,
error handling. They prove nothing about whether ST-Link enumerates or whether
BootCommander likes our arguments.

So: **start with `doctor`, then a `--dry-run`, then flash one board.** Do not
start by flashing three boards.

Specifically untested, in rough order of risk:

| Risk | Why | First thing to check |
|---|---|---|
| The whole TUI | Windows Python has no `_curses`, so `fabrica/tui.py` has never been executed | Run `./fabrica_cli.py tui`. If it misbehaves, every function is available as a plain CLI subcommand — use those and carry on |
| openocd + `.srec` | openocd picks its image parser from the file extension and knows `.s19`, not `.srec`. Given an unrecognised extension it falls back to **raw binary**, which would write the ASCII text of the S-record into flash — silent corruption, not an error. We therefore emit explicit `flash write_image ... s19` rather than the usual `program` helper | Only matters if `STM32_Programmer_CLI` is absent. Prefer installing STM32CubeProgrammer |
| `open_bus("can0")` | A one-line wrapper around `can.Bus`, and the one line no test can reach — the tests use the `virtual` backend | `./fabrica_cli.py monitor --seconds 5` |
| Extended 29-bit CAN | Encoded as `-xid=1` to match `flash_can.sh`. Never exercised by anyone | Nothing to check: all four boards are 11-bit standard |
| `-s=xcp` and `-t1` | `-s=xcp` matches your working script. `-t1` is **not** passed by default, so the argv is byte-for-byte the invocation already proven on the bench | If BootCommander rejects an option, `-s=xcp` is the only non-script flag |

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
- **STM32CubeProgrammer** is a manual download from ST. `stlink-tools` is the
  apt fallback, but prefer CubeProgrammer given the openocd `.srec` risk above.

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

**8. Only now repeat for the other boards**, and consider the TUI:

```bash
./fabrica_cli.py tui
```

---

## Commands

```
setup [--install]             detect host, check/install dependencies
doctor                        check tools, CAN link, firmware checksums
list                          boards, CAN ids, image sizes, provenance
flash <board> boot|app        flash via ST-Link (boot) or CAN (app)
reset <board>                 send the bootloader trigger frame
monitor [--board B]           listen and decode, --seconds N
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
q        quit
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

## Regenerating firmware

```bash
python vv/run_gate.py --stage-artifacts
```

Writes `firmware/<board>/*.srec` and `firmware/manifest.json`. The binaries are
deliberately not tracked in git — attach them to a tagged release instead.

## Tests

```bash
python -m pytest Tools/fabrica/tests -q     # 177 tests, no hardware needed
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
