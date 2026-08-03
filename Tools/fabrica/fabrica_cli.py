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
    ./fabrica_cli.py reset powerstage       # trigger the bootloader
    ./fabrica_cli.py monitor --seconds 10   # live decode
    ./fabrica_cli.py verify powerstage --allow-transmit   # HIL checks
    ./fabrica_cli.py tui                    # full screen interface

Add --dry-run to any flash to see the exact command without touching hardware.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

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


def _resolve_firmware(args):
    """Load the manifest for --firmware, accepting a loose folder too."""
    from fabrica import sources
    if args.firmware:
        found = sources.discover(args.firmware, max_depth=0)
        if found:
            return sources.load(found[0])
    return mf.load_manifest(args.firmware)


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


def cmd_flash(args) -> int:
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
                           board.mcu, dry_run=args.dry_run, on_output=echo)
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
                             dry_run=args.dry_run, on_output=echo)

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


# -------------------------------------------------------------- monitor ----
def cmd_monitor(args) -> int:
    from fabrica import canbus
    db = None
    if args.board:
        # Only now is a manifest needed - it is what maps a board to its DBC.
        # Plain listening must not require one: watching the bus is the first
        # thing you do on a fresh bench, before any firmware has been staged.
        board = _resolve_firmware(args).board(args.board)
        dbc = canbus.find_dbc(board.dbc) if board.dbc else None
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
        path = canbus.find_dbc(board.dbc)
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
                                 seconds=args.seconds)
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


# ------------------------------------------------------------------ tui ----
def cmd_tui(args) -> int:
    from fabrica.tui import run_tui
    return run_tui(args.firmware, args.iface, args.bitrate)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="fabrica", description="Fabrica firmware bench tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("\n\n", 1)[1])
    p.add_argument("--firmware", help="firmware directory (default: ./firmware)")
    p.add_argument("--iface", default="can0", help="CAN interface (default: can0)")
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

    f = sub.add_parser("flash", help="flash a board")
    f.add_argument("board")
    f.add_argument("kind", choices=["boot", "app"])
    f.add_argument("--dry-run", action="store_true")
    f.set_defaults(func=cmd_flash)

    r = sub.add_parser("reset", help="send the bootloader trigger frame")
    r.add_argument("board")
    r.add_argument("--dry-run", action="store_true")
    r.set_defaults(func=cmd_reset)

    m = sub.add_parser("monitor", help="listen and decode")
    m.add_argument("--board", help="use this board's DBC to decode")
    m.add_argument("--seconds", type=float, default=10.0)
    m.set_defaults(func=cmd_monitor)

    v = sub.add_parser("verify", help="HIL check: broadcasts, causality, reset")
    v.add_argument("board")
    v.add_argument("--allow-transmit", action="store_true",
                   help="required: this sends frames on a possibly shared bus")
    v.add_argument("--include-reset", action="store_true",
                   help="also send the bootloader trigger, stopping the board")
    v.add_argument("--seconds", type=float, default=3.0)
    v.set_defaults(func=cmd_verify)

    sub.add_parser("tui", help="full-screen interface").set_defaults(func=cmd_tui)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
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
