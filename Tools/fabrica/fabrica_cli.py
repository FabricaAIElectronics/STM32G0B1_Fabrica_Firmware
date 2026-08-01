#!/usr/bin/env python3
"""Fabrica bench tool - command line interface.

Everything the TUI does is available here as a plain subcommand. That is
deliberate: this tool was written without access to the bench, so if the curses
UI misbehaves on an unfamiliar terminal, the CLI is the fallback that still
flashes boards.

    ./fabrica_cli.py doctor                 # check the environment first
    ./fabrica_cli.py list                   # what can I flash
    ./fabrica_cli.py flash powerstage boot  # bootloader, via ST-Link
    ./fabrica_cli.py flash powerstage app   # application, over CAN
    ./fabrica_cli.py reset powerstage       # trigger the bootloader
    ./fabrica_cli.py monitor --seconds 10   # live decode
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


# --------------------------------------------------------------- doctor ----
def cmd_doctor(args) -> int:
    e = env.doctor(args.firmware, iface=args.iface, bitrate=args.bitrate)
    width = max(len(c.name) for c in e.checks)
    for c in e.checks:
        print(f"  {_MARK[c.status]:>16}  {c.name.ljust(width)}  {c.detail}")
        if c.remedy and c.status != env.OK:
            print(f"  {'':>16}  {' ' * width}  {DIM}-> {c.remedy}{RESET}")
    print()
    if e.ok:
        print(f"{GREEN}Environment looks usable.{RESET}")
        return 0
    print(f"{RED}Fix the FAIL rows above before flashing.{RESET}")
    return 1


# ----------------------------------------------------------------- list ----
def cmd_list(args) -> int:
    man = mf.load_manifest(args.firmware)
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
    man = mf.load_manifest(args.firmware)
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
    man = mf.load_manifest(args.firmware)
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
    man = mf.load_manifest(args.firmware)
    db = None
    if args.board:
        board = man.board(args.board)
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

    sub.add_parser("doctor", help="check tools, CAN link and firmware").set_defaults(
        func=cmd_doctor)
    sub.add_parser("list", help="list boards in the manifest").set_defaults(
        func=cmd_list)

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
    except KeyboardInterrupt:
        print("\ninterrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main())
