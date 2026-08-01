"""Curses front end for the Fabrica bench tool.

stdlib curses on purpose: it works over a plain SSH session to a headless
Jetson or RPi with no extra packages, and it is the same idiom can_decoder.py
already uses.

Layout:

    +--------------------------+-----------------------------------+
    | boards (select with j/k) | live telemetry, decoded via DBC   |
    +--------------------------+-----------------------------------+
    | log - every command run, every result                         |
    +---------------------------------------------------------------+
    | status: interface, manifest provenance, key hints             |
    +---------------------------------------------------------------+

Flashing runs in a worker thread so the UI keeps redrawing; output lines are
pushed onto a queue the main loop drains. Nothing else is threaded.
"""
from __future__ import annotations

import curses
import queue
import threading
import time
from pathlib import Path

from . import canbus, canflash, env, manifest as mf, stlink

HELP = "j/k select  b=flash boot  a=flash app  r=reset  m=monitor  d=doctor  q=quit"


class App:
    def __init__(self, firmware_dir, iface: str, bitrate: int):
        self.manifest = mf.load_manifest(firmware_dir)
        self.iface = iface
        self.bitrate = bitrate
        self.sel = 0
        self.log: list[tuple[str, str]] = []
        self.events: queue.Queue = queue.Queue()
        self.busy = False
        self.monitor: canbus.Monitor | None = None
        self.bus = None
        self.monitoring = False
        self.say("info", f"loaded manifest: git {self.manifest.git_sha[:8]}"
                         f"{' DIRTY' if self.manifest.git_dirty else ''}")
        if self.manifest.git_dirty:
            self.say("warn", "images were staged from a dirty tree - "
                             "provenance is not reproducible")

    # ------------------------------------------------------------ state --
    @property
    def board(self) -> mf.BoardImages:
        return self.manifest.boards[self.sel]

    def say(self, level: str, text: str) -> None:
        self.log.append((level, text))
        del self.log[:-400]

    # ----------------------------------------------------------- actions --
    def _worker(self, fn) -> None:
        """Run a blocking action off the UI thread, funnelling output to the queue."""
        self.busy = True

        def run():
            try:
                fn(lambda line: self.events.put(("out", line.rstrip())))
            except Exception as exc:                     # noqa: BLE001
                self.events.put(("err", f"{type(exc).__name__}: {exc}"))
            finally:
                self.events.put(("done", ""))

        threading.Thread(target=run, daemon=True).start()

    def flash_boot(self) -> None:
        board = self.board
        backend, exe = env.find_stlink()
        if not backend:
            self.say("err", "no ST-Link backend found - press d for doctor")
            return
        try:
            mf.verify_image(self.manifest, board.boot)
        except mf.ManifestError as exc:
            self.say("err", str(exc).splitlines()[0])
            return
        path = self.manifest.path_of(board.boot)
        self.say("info", f"flashing {board.name} bootloader via {backend}")
        self._worker(lambda cb: stlink.flash(
            backend, exe, path, board.boot.load_addr_int, board.mcu,
            on_output=cb))

    def flash_app(self) -> None:
        board = self.board
        exe = env.find_bootcommander()
        if not exe:
            self.say("err", "BootCommander not found - press d for doctor")
            return
        try:
            mf.verify_image(self.manifest, board.app)
        except mf.ManifestError as exc:
            self.say("err", str(exc).splitlines()[0])
            return
        path = self.manifest.path_of(board.app)
        self.say("info", f"flashing {board.name} app over CAN "
                         f"tid=0x{board.blt_rx:03X} rid=0x{board.blt_tx:03X}")
        self._worker(lambda cb: canflash.flash(
            exe, self.iface, board.bitrate, board.blt_rx, board.blt_tx, path,
            extended=board.extended, on_output=cb))

    def send_reset(self) -> None:
        board = self.board
        try:
            bus = canbus.open_bus(self.iface)
            try:
                canbus.send_reset(bus, board.blt_rx)
            finally:
                bus.shutdown()
            self.say("ok", f"bootloader trigger sent to 0x{board.blt_rx:03X}")
        except Exception as exc:                          # noqa: BLE001
            self.say("err", f"reset failed: {exc}")

    def toggle_monitor(self) -> None:
        if self.monitoring:
            self.monitoring = False
            if self.bus:
                self.bus.shutdown()
                self.bus = None
            self.say("info", "monitor stopped")
            return
        board = self.board
        db = None
        if board.dbc:
            path = canbus.find_dbc(board.dbc)
            if path:
                try:
                    db = canbus.load_dbc(path)
                except Exception as exc:                  # noqa: BLE001
                    self.say("warn", f"DBC load failed: {exc}")
        if db is None:
            self.say("warn", f"no DBC for {board.id} - raw frames only")
        try:
            self.bus = canbus.open_bus(self.iface)
        except Exception as exc:                          # noqa: BLE001
            self.say("err", f"cannot open {self.iface}: {exc}")
            return
        self.monitor = canbus.Monitor(db)
        self.monitoring = True
        self.say("ok", f"monitoring {self.iface}")

    def pump_monitor(self) -> None:
        if not (self.monitoring and self.bus):
            return
        for _ in range(200):                # bounded so the UI stays responsive
            msg = self.bus.recv(timeout=0.0)
            if msg is None:
                return
            self.monitor.observe(msg.arbitration_id, bytes(msg.data),
                                 msg.timestamp)

    def run_doctor(self) -> None:
        e = env.doctor(self.manifest.root, self.iface, self.bitrate)
        for c in e.checks:
            level = {"ok": "ok", "warn": "warn", "fail": "err"}[c.status]
            self.say(level, f"{c.name}: {c.detail}")
            if c.remedy and c.status != env.OK:
                self.say("info", f"    -> {c.remedy}")

    def drain(self) -> None:
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                return
            if kind == "done":
                self.busy = False
                self.say("info", "command finished")
            elif kind == "err":
                self.say("err", payload)
            elif payload:
                self.say("out", payload)


# ------------------------------------------------------------- rendering --
def _colour(level: str) -> int:
    return {"ok": curses.color_pair(1), "warn": curses.color_pair(2),
            "err": curses.color_pair(3), "info": curses.color_pair(4),
            "out": curses.A_DIM}.get(level, curses.A_NORMAL)


def _draw(stdscr, app: App) -> None:
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    if h < 12 or w < 60:
        stdscr.addnstr(0, 0, "terminal too small (need 60x12)", w - 1)
        stdscr.refresh()
        return

    left = max(26, w // 3)
    split = h - 12

    # boards
    stdscr.addnstr(0, 0, " BOARDS ".ljust(left - 1), left - 1, curses.A_REVERSE)
    for i, b in enumerate(app.manifest.boards):
        if 1 + i >= split:
            break
        attr = curses.A_REVERSE if i == app.sel else curses.A_NORMAL
        stdscr.addnstr(1 + i, 0, f" {b.id:<12} 0x{b.blt_rx:03X}", left - 1, attr)

    row = 1 + len(app.manifest.boards) + 1
    b = app.board
    for label, value in (("mcu", b.mcu), ("boot", f"{b.boot.flash_bytes} B"),
                         ("app", f"{b.app.flash_bytes} B"),
                         ("bitrate", str(b.bitrate)),
                         ("dbc", b.dbc or "none")):
        if row < split:
            stdscr.addnstr(row, 1, f"{label:<8}{value}", left - 2, curses.A_DIM)
            row += 1

    # telemetry
    stdscr.addnstr(0, left, " TELEMETRY ".ljust(w - left - 1), w - left - 1,
                   curses.A_REVERSE)
    if app.monitoring and app.monitor:
        frames = app.monitor.snapshot()
        counts = app.monitor.counts()
        r = 1
        for f in frames:
            if r >= split:
                break
            rate = app.monitor.rate_hz(f.arb_id)
            stdscr.addnstr(r, left + 1,
                           f"0x{f.arb_id:03X} {(f.name or 'raw'):<20} "
                           f"n={counts[f.arb_id]:<4} "
                           f"{(f'{rate:4.1f}Hz' if rate else '   -  ')} "
                           f"{f.raw.hex(' ')}", w - left - 2)
            r += 1
            for k, v in list(f.signals.items())[:3]:
                if r >= split:
                    break
                stdscr.addnstr(r, left + 3, f"{k} = {v}", w - left - 4,
                               curses.A_DIM)
                r += 1
        if not frames:
            stdscr.addnstr(1, left + 1, "waiting for traffic...", w - left - 2,
                           curses.A_DIM)
    else:
        stdscr.addnstr(1, left + 1, "monitor stopped - press m", w - left - 2,
                       curses.A_DIM)

    # log
    stdscr.addnstr(split, 0, " LOG ".ljust(w - 1), w - 1, curses.A_REVERSE)
    visible = app.log[-(h - split - 2):]
    for i, (level, text) in enumerate(visible):
        stdscr.addnstr(split + 1 + i, 0, text[:w - 1], w - 1, _colour(level))

    man = app.manifest
    status = (f" {app.iface} | git {man.git_sha[:8]}"
              f"{' DIRTY' if man.git_dirty else ''} | "
              f"{'BUSY' if app.busy else 'idle'} | {HELP}")
    stdscr.addnstr(h - 1, 0, status.ljust(w - 1)[:w - 1], w - 1, curses.A_REVERSE)
    stdscr.refresh()


def _loop(stdscr, app: App) -> int:
    curses.curs_set(0)
    stdscr.nodelay(True)
    curses.start_color()
    curses.use_default_colors()
    for i, fg in enumerate((curses.COLOR_GREEN, curses.COLOR_YELLOW,
                            curses.COLOR_RED, curses.COLOR_CYAN), start=1):
        curses.init_pair(i, fg, -1)

    while True:
        app.drain()
        app.pump_monitor()
        _draw(stdscr, app)

        ch = stdscr.getch()
        if ch == -1:
            time.sleep(0.05)
            continue
        key = chr(ch) if 0 <= ch < 256 else ""
        if key in ("q", "Q"):
            if app.bus:
                app.bus.shutdown()
            return 0
        if ch in (curses.KEY_DOWN,) or key == "j":
            app.sel = (app.sel + 1) % len(app.manifest.boards)
        elif ch in (curses.KEY_UP,) or key == "k":
            app.sel = (app.sel - 1) % len(app.manifest.boards)
        elif app.busy:
            app.say("warn", "a command is already running")
        elif key == "b":
            app.flash_boot()
        elif key == "a":
            app.flash_app()
        elif key == "r":
            app.send_reset()
        elif key == "m":
            app.toggle_monitor()
        elif key == "d":
            app.run_doctor()


def run_tui(firmware_dir: Path | str | None, iface: str, bitrate: int) -> int:
    app = App(firmware_dir, iface, bitrate)
    return curses.wrapper(_loop, app)
