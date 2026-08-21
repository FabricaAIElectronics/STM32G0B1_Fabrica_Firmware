#!/usr/bin/env python3
"""Fabrica bench tool - command line interface.

Everything the TUI does is available here as a plain subcommand. That is
deliberate: this tool was written without access to the bench, so if the curses
UI misbehaves on an unfamiliar terminal, the CLI is the fallback that still
flashes boards.

    ./fabrica_cli.py setup                  # what does this host still need
    ./fabrica_cli.py setup --install        # install it
    ./fabrica_cli.py doctor                 # check the environment
    ./fabrica_cli.py sources                # which firmware folders exist
    ./fabrica_cli.py list                   # what can I flash
    ./fabrica_cli.py flash powerstage boot  # bootloader, via ST-Link
    ./fabrica_cli.py flash powerstage app   # application, over CAN
    ./fabrica_cli.py flash all app          # every application, one at a time
    ./fabrica_cli.py reset powerstage       # trigger the bootloader
    ./fabrica_cli.py monitor --seconds 10   # live decode
    ./fabrica_cli.py verify powerstage --allow-transmit   # HIL checks
    ./fabrica_cli.py tui                    # full screen interface

Configuration - read, write, and make it survive a power cycle:

    ./fabrica_cli.py config read powerstage             # live and stored
    ./fabrica_cli.py config write powerstage Fan_Duty=70 --allow-transmit
    ./fabrica_cli.py config save powerstage --allow-transmit   # -> EEPROM
    ./fabrica_cli.py config defaults powerstage --allow-transmit
    ./fabrica_cli.py config dump powerstage             # board -> profile file
    ./fabrica_cli.py config diff powerstage             # profile vs board
    ./fabrica_cli.py config apply powerstage --allow-transmit --save

A write sets the live value. It is NOT persistent until `config save`, which is
a separate command on purpose: every board here distinguishes what it is doing
now from what it will come back to after a power cycle, and so does this tool.

Add --dry-run to any flash or config write to see exactly what would be sent
without touching hardware.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

# vendor/ holds python-can and cantools for benches with no network and no pip.
# Appended, not inserted, so a system install still wins - a vendored copy is a
# fallback, not an override, and silently shadowing the operator's own packages
# is the kind of thing that makes a version mismatch impossible to diagnose.
# Populate with:  pip install --target vendor -r requirements.txt
_VENDOR = _HERE / "vendor"
if _VENDOR.is_dir():
    sys.path.append(str(_VENDOR))

from fabrica import canflash, env, manifest as mf, stlink  # noqa: E402

GREEN, YELLOW, RED, DIM, RESET = (
    "\033[32m", "\033[33m", "\033[31m", "\033[2m", "\033[0m")
_MARK = {env.OK: f"{GREEN}ok{RESET}", env.WARN: f"{YELLOW}warn{RESET}",
         env.FAIL: f"{RED}FAIL{RESET}"}


def _no_colour():
    global GREEN, YELLOW, RED, DIM, RESET
    GREEN = YELLOW = RED = DIM = RESET = ""
    for k in _MARK:
        _MARK[k] = k.upper()


# -------------------------------------------------------------- sources ----
def _fmt_source(s, index=None) -> str:
    import datetime
    when = datetime.datetime.fromtimestamp(s.mtime).strftime("%Y-%m-%d %H:%M")
    tag = "verified" if s.trusted else f"{YELLOW}UNVERIFIED{RESET}"
    prefix = f"  [{index}] " if index is not None else "  "
    return (f"{prefix}{when}  {s.path.name:<28} {s.srec_count} srec "
            f"{s.dbc_count} dbc  {tag}")


def cmd_sources(args) -> int:
    """List selectable firmware folders, newest first."""
    from fabrica import sources
    root = args.root or (args.firmware or str(mf.DEFAULT_FIRMWARE_DIR.parent))
    found = sources.discover(root)
    if not found:
        print(f"no firmware folders under {root}")
        return 1
    print(f"firmware folders under {root}  (newest first)\n")
    for i, s in enumerate(found):
        print(_fmt_source(s, i))
        print(f"        boards: {', '.join(s.boards) or '?'}"
              + (f"   config: {s.config.name}" if s.config else ""))
        for n in s.notes:
            print(f"        {DIM}note: {n}{RESET}")
    print(f"\n{DIM}Use --firmware <path> to select one.{RESET}")
    return 0


#: Where to look for firmware when --firmware is not given, best first.
#: The bench keeps its images outside the checkout (so a git clean cannot
#: delete the thing it is about to flash), which is why the tool's own
#: ./firmware is not enough on its own.
def _firmware_candidates() -> list[Path]:
    here = Path(__file__).resolve().parent
    return [
        here / "firmware",                 # staged by the V&V gate
        Path.cwd() / "firmware",           # run from a bench directory
        Path.home() / "fabrica-bench" / "firmware",
    ]


def find_firmware_dir() -> Path | None:
    """Newest firmware set under the candidate directories, or None.

    Returns the set itself, not the directory holding it. firmware/ is a
    container of named versions that the operator copies in - so pointing the
    tool at firmware/ would either fail or, worse, silently pick one. discover()
    already sorts newest-modified first, so [0] is the most recent version, and
    `f` in the TUI lists the rest.

    Resolved once in main() and written back into args.firmware, so every
    subcommand - including doctor, which does its own manifest check - sees the
    same directory. Resolving it per-command is how doctor ended up reporting
    "no manifest" against the tool's own ./firmware while flash and verify were
    happily using the bench's.
    """
    from fabrica import sources
    for candidate in _firmware_candidates():
        if not candidate.is_dir():
            continue
        found = sources.discover(candidate, max_depth=2)
        if found:
            return found[0].path
    return None


def _resolve_firmware(args):
    """Load the manifest for --firmware, accepting a loose folder too."""
    from fabrica import sources

    if args.firmware:
        # Depth 0 first: an explicit path to one version folder must select
        # exactly that, never a sibling.
        found = sources.discover(args.firmware, max_depth=0)
        if found:
            return sources.load(found[0])
        # Then treat it as a container of versions and take the newest, so
        # `--firmware .../firmware` works as naturally as naming a version.
        found = sources.discover(args.firmware, max_depth=2)
        if found:
            return sources.load(found[0])
        return mf.load_manifest(args.firmware)

    tried = "\n  ".join(str(c) for c in _firmware_candidates())
    raise mf.ManifestError(
        "no firmware found. Looked in:\n  " + tried +
        "\n\nStage a build with:  python vv/run_gate.py --stage-artifacts"
        "\nor point at a folder: --firmware <dir>")


# ---------------------------------------------------------------- setup ----
def cmd_setup(args) -> int:
    """Check dependencies for this host and optionally install them."""
    from fabrica import host as hostmod

    plan = hostmod.build_plan()
    h = plan.host
    print(f"host      {h.description}")
    if h.model:
        print(f"model     {h.model}")
    print(f"detected  {h.kind}\n")

    if plan.empty:
        print(f"{GREEN}All package dependencies are already installed.{RESET}")
    else:
        if plan.apt:
            print(f"missing apt packages : {', '.join(plan.apt)}")
        if plan.pip:
            print(f"missing pip packages : {', '.join(plan.pip)}")
        print("\nCommands:")
        for c in plan.commands:
            print(f"  {c}")

    for w in plan.warnings:
        print(f"\n{YELLOW}note:{RESET} {w}")

    if h.can_hint:
        print(f"\n{DIM}CAN interface on this host:{RESET}\n    {h.can_hint}")
    for n in h.notes:
        print(f"\n{DIM}- {n}{RESET}")

    if not args.install:
        if not plan.empty:
            print(f"\n{DIM}Re-run with --install to execute the commands "
                  f"above.{RESET}")
        return 0

    if not h.is_linux:
        print(f"\n{RED}--install only works on Linux.{RESET}")
        return 1
    import subprocess
    for c in plan.commands:
        print(f"\n$ {c}")
        rc = subprocess.run(c, shell=True).returncode
        if rc != 0:
            print(f"{RED}failed (exit {rc}){RESET}")
            return rc
    print(f"\n{GREEN}Done. Run `doctor` next.{RESET}")
    return 0


# --------------------------------------------------------------- doctor ----
def cmd_doctor(args) -> int:
    from fabrica import host as hostmod
    h = hostmod.detect_host()
    print(f"{DIM}host: {h.description}{RESET}\n")

    e = env.doctor(args.firmware, iface=args.iface, bitrate=args.bitrate)
    width = max(len(c.name) for c in e.checks)
    for c in e.checks:
        print(f"  {_MARK[c.status]:>16}  {c.name.ljust(width)}  {c.detail}")
        if c.remedy and c.status != env.OK:
            print(f"  {'':>16}  {' ' * width}  {DIM}-> {c.remedy}{RESET}")
    print()
    can_bad = any(c.name.startswith("can") and c.status == env.FAIL
                  for c in e.checks)
    if can_bad and h.can_hint:
        print(f"{DIM}CAN on this host ({h.kind}):{RESET}\n    {h.can_hint}\n")
    if e.ok:
        print(f"{GREEN}Environment looks usable.{RESET}")
        return 0
    print(f"{RED}Fix the FAIL rows above before flashing.{RESET}  "
          f"`setup` lists the install commands for this host.")
    return 1


# ----------------------------------------------------------------- list ----
def cmd_list(args) -> int:
    man = _resolve_firmware(args)
    print(f"manifest  git {man.git_sha[:8]}"
          f"{'  (DIRTY)' if man.git_dirty else ''}  gate={man.gate}")
    print(f"          generated {man.generated}\n")
    for b in man.boards:
        print(f"  {b.id:<12} {b.name:<18} {b.mcu}")
        print(f"  {'':<12} bootloader CAN 0x{b.blt_rx:03X}/0x{b.blt_tx:03X} "
              f"@ {b.bitrate} bps{'  [29-bit]' if b.extended else ''}")
        for kind in ("boot", "app"):
            img = b.image(kind)
            print(f"  {'':<12}   {kind:<5} {img.flash_bytes:>7} B @ {img.load_addr}"
                  f"  {img.file}")
        if b.address_plan_exempt:
            print(f"  {'':<12}   {DIM}address-plan exempt (ids owned by another "
                  f"team){RESET}")
        print()
    return 0


# ---------------------------------------------------------------- flash ----
def _verified_image(man: mf.Manifest, board: mf.BoardImages, kind: str) -> Path:
    img = board.image(kind)
    mf.verify_image(man, img)          # raises ManifestError on mismatch
    return man.path_of(img)


#: Flashed last in a `flash all`. On the shared internal bus this board's CAN
#: relay is just another rail, but it is the one board that can gate the bus it
#: is being flashed over, so it costs nothing to leave it until the others are
#: done and everything else is already verified.
_FLASH_LAST = ("powerstage",)


def flash_all_order(board_ids) -> list[str]:
    ids = [b for b in board_ids if b not in _FLASH_LAST]
    return ids + [b for b in board_ids if b in _FLASH_LAST]


def cmd_flash_all(args) -> int:
    """Flash every board's application over CAN, one at a time.

    Applications only. A bootloader goes over ST-Link, one physical probe per
    board, so there is no unattended "flash all bootloaders" to offer.

    Strictly sequential, and it stops at the first failure unless told not to.
    Both matter for the same reason: when a target does not answer,
    BootCommander's backdoor poll transmits XCP CONNECT continuously and does
    not give up on its own. One left running has already flooded this bus for
    five minutes and made an unrelated board miss its reset trigger. Flashing
    the next board into that is not a recovery, it is a second failure with a
    misleading cause.
    """
    from fabrica import canbus

    man = _resolve_firmware(args)
    order = flash_all_order(man.board_ids)

    exe = env.find_bootcommander()
    if not exe:
        if not args.dry_run:
            print(f"{RED}BootCommander not found. Run `doctor`.{RESET}")
            return 1
        exe = env.BOOTCOMMANDER_CANDIDATES[1]
        print(f"{YELLOW}BootCommander not installed; showing commands for "
              f"{exe}{RESET}")

    print(f"Flashing {len(order)} application(s) over CAN on {args.iface}: "
          f"{', '.join(order)}\n")

    results: list[tuple[str, str]] = []
    for board_id in order:
        board = man.board(board_id)
        print(f"{DIM}--- {board.name} ---{RESET}")
        try:
            path = _verified_image(man, board, "app")
        except mf.ManifestError as e:
            print(f"{RED}checksum/manifest problem: {e}{RESET}")
            results.append((board_id, "FAILED"))
            if not args.keep_going:
                break
            continue

        if args.dry_run:
            res = canflash.flash(exe, args.iface, board.bitrate, board.blt_rx,
                                 board.blt_tx, path, extended=board.extended,
                                 dry_run=True)
            print("  " + " ".join(res.command))
            results.append((board_id, "dry-run"))
            continue

        # A running application answers this by resetting into its bootloader;
        # a board already in its bootloader treats the same frame as the XCP
        # CONNECT. Safe either way, and it saves BootCommander from having to
        # poll its way in.
        bus = canbus.open_bus(args.iface)
        try:
            canbus.send_reset(bus, board.blt_rx)
        finally:
            bus.shutdown()
        time.sleep(args.settle)

        res = canflash.flash(exe, args.iface, board.bitrate, board.blt_rx,
                             board.blt_tx, path, extended=board.extended,
                             on_output=lambda line: print(f"    {line.rstrip()}"),
                             on_start=_adopt_child)
        if res.ok:
            print(f"  {GREEN}OK{RESET}")
            results.append((board_id, "OK"))
            continue

        print(f"  {RED}FAILED{RESET} (exit {res.returncode})")
        results.append((board_id, "FAILED"))
        if not args.keep_going:
            print(f"\n{YELLOW}Stopping here.{RESET} A failed flash can leave "
                  f"the bus busy, so the next board's result would not be "
                  f"trustworthy. Fix this one, or re-run with --keep-going.")
            break

    print()
    for board_id, outcome in results:
        mark = {"OK": f"{GREEN}OK{RESET}", "FAILED": f"{RED}FAILED{RESET}"}.get(
            outcome, f"{DIM}{outcome}{RESET}")
        print(f"  {mark:20} {board_id}")
    not_reached = [b for b in order if b not in {r[0] for r in results}]
    if not_reached:
        print(f"  {DIM}not attempted:{RESET} {', '.join(not_reached)}")

    failed = [r for r in results if r[1] == "FAILED"]
    if failed:
        print(f"\n{RED}{len(failed)} board(s) failed.{RESET}")
        return 1
    if args.dry_run:
        print(f"\n{YELLOW}DRY RUN - nothing was executed.{RESET}")
    else:
        print(f"\n{GREEN}All {len(results)} application(s) flashed.{RESET}")
    return 0


def cmd_flash(args) -> int:
    if args.board == "all":
        if args.kind != "app":
            print(f"{RED}`flash all boot` is not offered.{RESET}")
            print("  A bootloader goes over ST-Link, one physical probe per "
                  "board, so it cannot be done unattended.")
            print("  Flash them one at a time: "
                  "`flash <board> boot`.")
            return 2
        return cmd_flash_all(args)

    man = _resolve_firmware(args)
    board = man.board(args.board)
    path = _verified_image(man, board, args.kind)
    print(f"{DIM}checksum verified: {board.image(args.kind).sha256[:16]}...{RESET}")

    def echo(line: str) -> None:
        print(f"  {line.rstrip()}")

    if args.kind == "boot":
        backend, exe = env.find_stlink()
        if not backend:
            if not args.dry_run:
                print(f"{RED}No ST-Link backend found. Run `doctor`.{RESET}")
                return 1
            # A dry run must still show the command. Being able to review the
            # exact argv on a machine with no toolchain is the whole point.
            backend, exe = env.STLINK_BACKENDS[0], env.STLINK_BACKENDS[0]
            print(f"{YELLOW}no ST-Link backend installed; showing the command "
                  f"for {backend}{RESET}")
        print(f"Flashing {board.name} bootloader via {backend} ...")
        res = stlink.flash(backend, exe, path, board.boot.load_addr_int,
                           board.mcu, dry_run=args.dry_run, on_output=echo,
                           on_start=_adopt_child)
    else:
        exe = env.find_bootcommander()
        if not exe:
            if not args.dry_run:
                print(f"{RED}BootCommander not found. Run `doctor`.{RESET}")
                return 1
            exe = env.BOOTCOMMANDER_CANDIDATES[1]   # /opt/openblt/Host/...
            print(f"{YELLOW}BootCommander not installed; showing the command "
                  f"for {exe}{RESET}")
        print(f"Flashing {board.name} application over CAN "
              f"(tid 0x{board.blt_rx:03X} / rid 0x{board.blt_tx:03X}) ...")
        res = canflash.flash(exe, args.iface, board.bitrate, board.blt_rx,
                             board.blt_tx, path, extended=board.extended,
                             dry_run=args.dry_run, on_output=echo,
                             on_start=_adopt_child)

    if args.dry_run:
        print(f"\n{YELLOW}DRY RUN - nothing was executed.{RESET}")
        print("  " + " ".join(res.command))
        return 0
    if res.ok:
        print(f"\n{GREEN}OK{RESET}  {board.name} {args.kind} flashed.")
        return 0
    print(f"\n{RED}FAILED{RESET} (exit {res.returncode})")
    print("  " + " ".join(res.command))
    return 1


# ---------------------------------------------------------------- reset ----
def cmd_reset(args) -> int:
    from fabrica import canbus
    man = _resolve_firmware(args)
    board = man.board(args.board)
    arb_id, payload = canflash.build_reset_frame(board.blt_rx)
    if args.dry_run:
        print(f"DRY RUN: would send id=0x{arb_id:03X} data={payload.hex(' ')} "
              f"on {args.iface}")
        return 0
    bus = canbus.open_bus(args.iface)
    try:
        canbus.send_reset(bus, board.blt_rx)
    finally:
        bus.shutdown()
    print(f"{GREEN}sent{RESET} bootloader trigger id=0x{arb_id:03X} "
          f"data={payload.hex(' ')}")
    return 0


# -------------------------------------------------------------- release ----
def cmd_release(args) -> int:
    """Free a target a debugger is still holding.

    Needed because a held core is indistinguishable from a dead board: silent
    on CAN, normal current draw, and un-flashable over CAN because the
    application that would answer XCP is not running. The documented escape used
    to be a power cycle.
    """
    backend, path = stlink.find_stlink()
    if backend != "openocd":
        print(f"{RED}release needs openocd{RESET}; found "
              f"{backend or 'no ST-Link backend'}. Power-cycle the board "
              f"instead.")
        return 1

    man = _resolve_firmware(args)
    board = man.board(args.board)
    print(f"releasing {board.name} ({board.mcu}) ...")
    result = stlink.release(path, board.mcu, on_output=lambda l: print("  " + l))
    if result.ok:
        print(f"{GREEN}OK{RESET}  target resumed and probe detached.")
        return 0
    print(f"{RED}FAILED{RESET} (exit {result.returncode}) - power-cycle the board.")
    return 1


# -------------------------------------------------------------- monitor ----
def cmd_monitor(args) -> int:
    from fabrica import canbus
    db = None
    if args.board:
        # Only now is a manifest needed - it is what maps a board to its DBC.
        # Plain listening must not require one: watching the bus is the first
        # thing you do on a fresh bench, before any firmware has been staged.
        man = _resolve_firmware(args)
        board = man.board(args.board)
        dbc = canbus.find_dbc(board.dbc, man.root) if board.dbc else None
        if dbc:
            db = canbus.load_dbc(dbc)
            print(f"{DIM}decoding with {dbc.name}{RESET}")
        else:
            print(f"{YELLOW}no DBC for {board.id}; showing raw frames{RESET}")

    monitor = canbus.Monitor(db)
    bus = canbus.open_bus(args.iface)
    deadline = time.time() + args.seconds
    print(f"listening on {args.iface} for {args.seconds}s ...\n")
    try:
        while time.time() < deadline:
            msg = bus.recv(timeout=0.5)
            if msg is not None:
                monitor.observe(msg.arbitration_id, bytes(msg.data),
                                msg.timestamp)
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()

    frames = monitor.snapshot()
    if not frames:
        print(f"{YELLOW}no traffic seen.{RESET} Check wiring, termination, "
              f"and that the board is powered.")
        return 1
    counts = monitor.counts()
    for f in frames:
        rate = monitor.rate_hz(f.arb_id)
        rate_s = f"{rate:5.1f} Hz" if rate else "   -   "
        print(f"  0x{f.arb_id:03X}  {(f.name or '?'):<22} "
              f"n={counts[f.arb_id]:<5} {rate_s}  {f.raw.hex(' ')}")
        for k, v in list(f.signals.items())[:6]:
            print(f"         {DIM}{k} = {v}{RESET}")
    return 0


# --------------------------------------------------------------- verify ----
def cmd_verify(args) -> int:
    """Hardware-in-the-loop check of the three behavioural properties."""
    from fabrica import canbus, verify

    man = _resolve_firmware(args)
    board = man.board(args.board)

    if not args.allow_transmit:
        print(f"{YELLOW}verify TRANSMITS on {args.iface}.{RESET}")
        print("  It sends a configuration command, and with --include-reset a")
        print("  bootloader trigger that stops the application.")
        print("  Other equipment may share this bus. Re-run with --allow-transmit.")
        return 1

    db = None
    if board.dbc:
        path = canbus.find_dbc(board.dbc, man.root)
        if path:
            db = canbus.load_dbc(path)
    expected = verify.expected_broadcast_ids(db, board.blt_tx)
    if not expected:
        print(f"{YELLOW}no DBC for {board.id}; cannot tell which broadcasts to "
              f"expect.{RESET}")
        return 1

    print(f"verifying {board.name} on {args.iface} "
          f"({len(expected)} expected broadcasts)\n")
    bus = canbus.open_bus(args.iface)
    try:
        results = verify.run_all(bus, board, db, expected,
                                 include_reset=args.include_reset,
                                 seconds=args.seconds,
                                 allow_actuate=args.allow_actuate)
    finally:
        bus.shutdown()

    mark = {verify.PASS: f"{GREEN}PASS{RESET}", verify.FAIL: f"{RED}FAIL{RESET}",
            verify.SKIP: f"{DIM}SKIP{RESET}"}
    for r in results:
        print(f"  {mark[r.status]}  {r.name:12} {r.detail}")
    failed = [r for r in results if r.status == verify.FAIL]
    print()
    if failed:
        print(f"{RED}{len(failed)} property/properties failed.{RESET}")
        return 1
    print(f"{GREEN}All checked properties hold.{RESET}")
    return 0


# ----------------------------------------------------------------- config --
#: Profiles live beside the tool, so `scp -r Tools/fabrica` carries a rig's
#: intended configuration with it. Tracked in git: a bench config that cannot
#: be reviewed or diffed is folklore.
PROFILE_DIR = _HERE / "profiles"


def _board_db(man, board):
    """The board's DBC, or None. Same resolution order as monitor and verify."""
    from fabrica import canbus
    if not board.dbc:
        return None
    path = canbus.find_dbc(board.dbc, man.root)
    return canbus.load_dbc(path) if path else None


def _parse_assignments(pairs: list[str]) -> dict:
    """``Fan_Duty=70`` -> ``{"Fan_Duty": 70}``. Hex accepted for masks."""
    out = {}
    for pair in pairs:
        if "=" not in pair:
            raise ValueError(f"expected SIGNAL=VALUE, got {pair!r}")
        name, _, raw = pair.partition("=")
        name, raw = name.strip(), raw.strip()
        try:
            out[name] = int(raw, 0)
        except ValueError:
            try:
                out[name] = float(raw)
            except ValueError:
                raise ValueError(
                    f"{name}: {raw!r} is not a number. Signals are written as "
                    f"numbers, including enumerated ones.") from None
    return out


def _status_mark(status: str) -> str:
    from fabrica import config as cfg
    return {cfg.MATCH: f"{GREEN}match{RESET}",
            cfg.DIFFER: f"{RED}differ{RESET}",
            cfg.UNKNOWN: f"{YELLOW}?{RESET}",
            cfg.NO_ECHO: f"{DIM}-{RESET}"}.get(status, status)


def _show(value) -> str:
    if value is None:
        return "-"
    if isinstance(value, float) and value == int(value):
        return str(int(value))
    return str(value)


def _config_context(args):
    """(manifest, board, db, bus) for a config subcommand. Caller closes bus."""
    from fabrica import canbus
    man = _resolve_firmware(args)
    board = man.board(args.board)
    db = _board_db(man, board)
    if db is None:
        raise SystemExit(
            f"no DBC for {board.id}: configuration is addressed by signal "
            f"name, so there is nothing to read or write without one.")
    return man, board, db, canbus.open_bus(args.iface)


def _require_transmit(args, what: str) -> bool:
    if args.allow_transmit:
        return True
    print(f"{YELLOW}{what} TRANSMITS on {args.iface}.{RESET}")
    print("  Other equipment may share this bus. Re-run with --allow-transmit.")
    return False


def cmd_config_read(args) -> int:
    from fabrica import config as cfg, params
    _, board, db, bus = _config_context(args)
    try:
        state = cfg.collect(bus, db, board.id, timeout=args.timeout)
    finally:
        bus.shutdown()

    if state.missing:
        print(f"{YELLOW}no frames seen for: {', '.join(state.missing)}{RESET}")
        print(f"{DIM}  those parameters read as unknown, not as zero{RESET}\n")

    for grp in params.for_board(board.id).groups:
        print(f"{grp.message}")
        if grp.note:
            print(f"  {DIM}{grp.note}{RESET}")
        for p in grp.params:
            v = state.get(p.signal)
            live = _show(v.live) if v and v.live_seen else "-"
            stored = _show(v.stored) if v and v.stored_seen else "-"
            flag = f" {YELLOW}[actuates]{RESET}" if p.actuates else ""
            print(f"    {p.signal:26} live={live:>10}  stored={stored:>10}{flag}")
        print()
    return 0


def cmd_config_write(args) -> int:
    from fabrica import config as cfg, params
    try:
        overrides = _parse_assignments(args.assignment)
    except ValueError as e:
        print(f"{RED}{e}{RESET}")
        return 2
    if not overrides:
        print(f"{RED}nothing to write{RESET}")
        return 2

    _, board, db, bus = _config_context(args)
    failures = 0
    try:
        by_message = cfg.group_overrides(board.id, overrides)
        actuating = [p for msg in by_message
                     for p in params.group(board.id, msg).params
                     if p.signal in by_message[msg] and p.actuates]
        if actuating and not args.allow_actuate:
            print(f"{YELLOW}these parameters drive hardware:{RESET}")
            for p in actuating:
                print(f"  {p.signal}: {p.actuates}")
            print("Re-run with --allow-actuate.")
            return 1
        if not args.dry_run and not _require_transmit(args, "config write"):
            return 1

        for message, values in by_message.items():
            results, _ = cfg.write_and_verify(bus, db, board, message, values,
                                              timeout=args.timeout,
                                              dry_run=args.dry_run)
            if args.dry_run:
                print(f"{YELLOW}DRY RUN{RESET} {message}: nothing was sent")
                continue
            for r in results:
                if r.signal not in values:
                    continue
                ok = f"{GREEN}OK{RESET}" if r.ok else f"{RED}FAILED{RESET}"
                print(f"  {ok} {r.signal:26} wrote={_show(r.wanted):>10}  "
                      f"live={_status_mark(r.live_status)} "
                      f"stored={_status_mark(r.stored_status)}")
                failures += 0 if r.ok else 1
    except (cfg.ConfigError, KeyError) as e:
        print(f"{RED}{e}{RESET}")
        return 1
    finally:
        bus.shutdown()

    if args.dry_run:
        return 0
    if failures:
        print(f"\n{RED}{failures} parameter(s) did not read back.{RESET}")
        return 1
    print(f"\n{GREEN}written.{RESET} Not yet persistent - "
          f"`config save {board.id}` writes it to EEPROM.")
    return 0


def cmd_config_save(args) -> int:
    from fabrica import config as cfg
    _, board, db, bus = _config_context(args)
    try:
        if not args.dry_run and not _require_transmit(args, "config save"):
            return 1
        arb_id, data = cfg.persist(bus, db, board, dry_run=args.dry_run)
    except cfg.ConfigError as e:
        print(f"{RED}{e}{RESET}")
        return 1
    finally:
        bus.shutdown()
    verb = "would send" if args.dry_run else "sent"
    print(f"{GREEN}{verb}{RESET} EEPROM save id=0x{arb_id:03X} "
          f"data={data.hex(' ')}")
    return 0


def cmd_config_defaults(args) -> int:
    from fabrica import config as cfg
    _, board, db, bus = _config_context(args)
    try:
        if not args.dry_run and not _require_transmit(args, "config defaults"):
            return 1
        arb_id, data = cfg.load_defaults(bus, db, board, dry_run=args.dry_run)
    except cfg.ConfigError as e:
        print(f"{RED}{e}{RESET}")
        return 1
    finally:
        bus.shutdown()
    verb = "would send" if args.dry_run else "sent"
    print(f"{GREEN}{verb}{RESET} load-defaults id=0x{arb_id:03X} "
          f"data={data.hex(' ')}")
    print(f"{DIM}  this overwrites the board's stored configuration{RESET}")
    return 0


def _profile_for(args, board_id: str) -> Path:
    from fabrica import config as cfg
    return Path(args.profile) if args.profile else cfg.profile_path(
        PROFILE_DIR, board_id)


def cmd_config_dump(args) -> int:
    """Capture what a board currently reports into a profile file."""
    from fabrica import config as cfg
    _, board, db, bus = _config_context(args)
    try:
        state = cfg.collect(bus, db, board.id, timeout=args.timeout)
    finally:
        bus.shutdown()
    if state.missing:
        print(f"{YELLOW}not captured (no frames seen): "
              f"{', '.join(state.missing)}{RESET}")
    values = state.as_dict(prefer=args.prefer)
    if not values:
        print(f"{RED}nothing read back; refusing to write an empty profile"
              f"{RESET}")
        return 1
    path = cfg.save_profile(_profile_for(args, board.id), board.id, values,
                            note=f"captured from {board.id} on {args.iface}")
    print(f"{GREEN}wrote{RESET} {len(values)} parameter(s) to {path}")
    return 0


def cmd_config_diff(args) -> int:
    from fabrica import config as cfg
    path = _profile_for(args, args.board)
    _, board, db, bus = _config_context(args)
    try:
        raw = cfg.load_profile(path)
        unknown = cfg.validate_profile(board.id, raw)
        if unknown:
            print(f"{RED}profile names unknown parameter(s): "
                  f"{', '.join(unknown)}{RESET}")
            return 1
        state = cfg.collect(bus, db, board.id, timeout=args.timeout)
    except cfg.ConfigError as e:
        print(f"{RED}{e}{RESET}")
        return 1
    finally:
        bus.shutdown()

    diffs = cfg.diff(board.id, raw, state)
    changed = [d for d in diffs if d.changed]
    for d in diffs:
        if d.changed:
            current = _show(d.current) if d.seen else "unknown"
            print(f"  {RED}~{RESET} {d.signal:26} board={current:>10}  "
                  f"profile={_show(d.wanted):>10}")
        else:
            print(f"  {GREEN}={RESET} {d.signal:26} {_show(d.current):>10}")
    print()
    if not changed:
        print(f"{GREEN}board matches {path.name}.{RESET}")
        return 0
    print(f"{YELLOW}{len(changed)} parameter(s) differ{RESET} - "
          f"`config apply {board.id}` writes them.")
    return 1


def cmd_config_apply(args) -> int:
    from fabrica import config as cfg
    path = _profile_for(args, args.board)
    _, board, db, bus = _config_context(args)
    try:
        raw = cfg.load_profile(path)
        if not args.dry_run and not _require_transmit(args, "config apply"):
            return 1
        report = cfg.apply_profile(bus, db, board, raw,
                                   changed_only=not args.all,
                                   allow_actuate=args.allow_actuate,
                                   timeout=args.timeout,
                                   dry_run=args.dry_run)
        if not args.dry_run and args.save and report.ok and report.written:
            cfg.persist(bus, db, board)
            report.persisted = True
    except cfg.ConfigError as e:
        print(f"{RED}{e}{RESET}")
        return 1
    finally:
        bus.shutdown()

    if args.dry_run:
        print(f"{YELLOW}DRY RUN{RESET} would write: "
              f"{', '.join(report.written) or '(nothing)'}")
        return 0
    for r in report.results:
        ok = f"{GREEN}OK{RESET}" if r.ok else f"{RED}FAILED{RESET}"
        print(f"  {ok} {r.signal:26} wrote={_show(r.wanted):>10}  "
              f"live={_status_mark(r.live_status)} "
              f"stored={_status_mark(r.stored_status)}")
    if report.skipped:
        print(f"{DIM}  skipped (already matching, or actuating without "
              f"--allow-actuate): {', '.join(report.skipped)}{RESET}")
    print()
    if not report.ok:
        print(f"{RED}some parameters did not read back.{RESET}")
        return 1
    if report.persisted:
        print(f"{GREEN}applied and saved to EEPROM.{RESET}")
    elif report.written:
        print(f"{GREEN}applied.{RESET} Add --save to persist it.")
    else:
        print(f"{GREEN}nothing to do - the board already matches.{RESET}")
    return 0


_CONFIG_ACTIONS = {
    "read": cmd_config_read,
    "write": cmd_config_write,
    "save": cmd_config_save,
    "defaults": cmd_config_defaults,
    "dump": cmd_config_dump,
    "diff": cmd_config_diff,
    "apply": cmd_config_apply,
}


def cmd_config(args) -> int:
    from fabrica import config as cfg
    # Resolved here rather than as an argparse default so the constant stays
    # the single source of truth and can be tuned without editing the parser.
    if args.timeout is None:
        args.timeout = cfg.DEFAULT_READ_TIMEOUT
    if args.assignment and args.action != "write":
        print(f"{RED}`config {args.action}` takes no SIGNAL=VALUE arguments"
              f"{RESET}")
        return 2
    return _CONFIG_ACTIONS[args.action](args)


# ------------------------------------------------------------------ tui ----
def cmd_tui(args) -> int:
    from fabrica.tui import run_tui
    return run_tui(args.firmware, args.iface, args.bitrate)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="fabrica", description="Fabrica firmware bench tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("\n\n", 1)[1])
    p.add_argument("--firmware", help="firmware directory (default: auto-detect)")
    p.add_argument("--iface", default=None,
                   help="CAN interface (default: the first one that is up)")
    p.add_argument("--bitrate", type=int, default=500000)
    p.add_argument("--no-colour", action="store_true")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("setup", help="check/install dependencies for this host")
    s.add_argument("--install", action="store_true",
                   help="actually run the install commands (Linux only)")
    s.set_defaults(func=cmd_setup)

    sub.add_parser("doctor", help="check tools, CAN link and firmware").set_defaults(
        func=cmd_doctor)
    sub.add_parser("list", help="list boards in the manifest").set_defaults(
        func=cmd_list)

    sc = sub.add_parser("sources", help="list selectable firmware folders")
    sc.add_argument("--root", help="directory to search (default: alongside firmware/)")
    sc.set_defaults(func=cmd_sources)

    f = sub.add_parser("flash", help="flash a board, or `all` for every app")
    f.add_argument("board", help="board id, or 'all' (applications only)")
    f.add_argument("kind", choices=["boot", "app"])
    f.add_argument("--dry-run", action="store_true")
    f.add_argument("--keep-going", action="store_true",
                   help="with `all`: carry on after a failure. Off by default "
                        "because a failed flash can leave the bus busy")
    f.add_argument("--settle", type=float, default=1.0,
                   help="seconds between the reset trigger and the flash")
    f.set_defaults(func=cmd_flash)

    r = sub.add_parser("reset", help="send the bootloader trigger frame")
    r.add_argument("board")
    r.add_argument("--dry-run", action="store_true")
    r.set_defaults(func=cmd_reset)

    rel = sub.add_parser(
        "release",
        help="resume a target a debugger left halted (instead of power-cycling)")
    rel.add_argument("board")
    rel.set_defaults(func=cmd_release)

    m = sub.add_parser("monitor", help="listen and decode")
    m.add_argument("--board", help="use this board's DBC to decode")
    m.add_argument("--seconds", type=float, default=10.0)
    m.set_defaults(func=cmd_monitor)

    v = sub.add_parser("verify", help="HIL check: broadcasts, causality, reset")
    v.add_argument("board")
    v.add_argument("--allow-transmit", action="store_true",
                   help="required: this sends frames on a possibly shared bus")
    v.add_argument("--allow-actuate", action="store_true",
                   help="permit a causal check whose command drives hardware "
                        "(KincoDrive: switches the high-side power rails)")
    v.add_argument("--include-reset", action="store_true",
                   help="also send the bootloader trigger, stopping the board")
    v.add_argument("--seconds", type=float, default=3.0)
    v.set_defaults(func=cmd_verify)

    c = sub.add_parser("config", help="read, write and persist board settings")
    c.add_argument("action", choices=sorted(_CONFIG_ACTIONS))
    c.add_argument("board")
    c.add_argument("assignment", nargs="*", metavar="SIGNAL=VALUE",
                   help="for `write`: one or more signal assignments")
    c.add_argument("--profile", help="profile file (default: profiles/<board>.json)")
    c.add_argument("--timeout", type=float, default=None,
                   help="seconds to wait for readback (default: 3.0; the "
                        "slowest board rotates its config broadcast ~1.5s)")
    c.add_argument("--prefer", choices=["live", "stored"], default="live",
                   help="for `dump`: which surface to capture")
    c.add_argument("--all", action="store_true",
                   help="for `apply`: write every parameter, not only those "
                        "that differ")
    c.add_argument("--save", action="store_true",
                   help="for `apply`: persist to EEPROM after a clean apply")
    c.add_argument("--allow-transmit", action="store_true",
                   help="required to write: this puts frames on a shared bus")
    c.add_argument("--allow-actuate", action="store_true",
                   help="permit writing parameters that drive hardware "
                        "(rail enables, LED outputs, the CAN relay)")
    c.add_argument("--dry-run", action="store_true")
    c.set_defaults(func=cmd_config)

    sub.add_parser("tui", help="full-screen interface").set_defaults(func=cmd_tui)
    return p


#: Every subcommand name. Used to decide whether a bare invocation should open
#: the TUI. A test pins this against the parser's real choices so the two
#: cannot drift - a subcommand missing from here would make `fab <that name>`
#: silently launch the TUI instead.
SUBCOMMANDS = ("setup", "doctor", "list", "sources", "flash", "reset",
               "release", "monitor", "verify", "config", "tui")

#: Subprocesses this run has started, so they can be killed on the way out.
_LIVE_CHILDREN: list = []


def _adopt_child(proc) -> None:
    _LIVE_CHILDREN.append(proc)


def _autodetect_iface() -> str:
    """First CAN interface that is actually up, else 'can0'.

    A bench with can0 (SocketCAN) and can1 (PeakCAN) should not need the
    operator to remember which is which, and a machine with only vcan0 should
    not be told to use a can0 that does not exist. Falling back to 'can0' keeps
    the old behaviour when nothing can be detected, so the error message still
    names a real interface rather than 'None'.
    """
    try:
        from fabrica import env
        for name in env.can_interfaces():
            if env.can_link_state(name).get("up"):
                return name
    except Exception:                                     # noqa: BLE001
        pass
    return "can0"


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)

    # Bare `fab` opens the TUI. It is the interface meant for humans, and
    # making them type a subcommand to reach it is backwards - scripts always
    # name one explicitly, so nothing else changes.
    #
    # The test is "did they name a subcommand", not "does every argument look
    # like a flag": `fab --iface can1` has a bare `can1` in argv and must still
    # reach the TUI rather than an argparse error about a missing command.
    if not any(a in SUBCOMMANDS for a in argv):
        if not any(a in ("-h", "--help") for a in argv):
            argv.append("tui")

    args = build_parser().parse_args(argv)

    # Kill any flasher we started if this process is interrupted or exits.
    # BootCommander is a separate OS process: without this, Ctrl-C at the wrong
    # moment - or simply quitting - leaves it polling and transmitting onto a
    # bus other boards are using.
    import atexit
    import subprocess as _sp

    def _reap():
        for proc in list(_LIVE_CHILDREN):
            if proc.poll() is None:
                try:
                    proc.kill()
                    proc.wait(timeout=2)
                except (OSError, _sp.SubprocessError):
                    pass
    atexit.register(_reap)

    if args.iface is None:
        args.iface = _autodetect_iface()
    if not args.firmware:
        found = find_firmware_dir()
        if found is not None:
            args.firmware = str(found)
    if args.no_colour or not sys.stdout.isatty():
        _no_colour()
    try:
        return args.func(args)
    except mf.ManifestError as exc:
        print(f"{RED}manifest error:{RESET} {exc}", file=sys.stderr)
        return 2
    except KeyError as exc:
        print(f"{RED}{exc}{RESET}", file=sys.stderr)
        return 2
    except (ValueError, FileNotFoundError) as exc:
        # Backend capability problems (st-flash cannot take .srec, an MCU with no
        # openocd target mapping) arrive as ValueError from build_command. The
        # message is already actionable; a traceback on top of it is just noise.
        print(f"{RED}cannot build the flash command:{RESET} {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\ninterrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main())
