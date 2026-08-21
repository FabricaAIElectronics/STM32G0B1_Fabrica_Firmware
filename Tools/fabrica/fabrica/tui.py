"""Curses front end for the Fabrica bench tool.

stdlib curses on purpose: it works over a plain SSH session to a headless
Jetson or RPi with no extra packages, and it is the same idiom can_decoder.py
already uses.

Layout:

    +--------------------------+-----------------------------------+
    | boards (select with j/k) | live telemetry, decoded via DBC,  |
    |                          | OR the config table (press c)     |
    +--------------------------+-----------------------------------+
    | log - every command run, every result                         |
    +---------------------------------------------------------------+
    | status: interface, manifest provenance, key hints             |
    +---------------------------------------------------------------+

The config screen shows three columns per parameter - desired, live, stored -
because they are three different facts. Live is what the board is doing now;
stored is what it comes back to after a power cycle. A write moves the first;
only an EEPROM save moves the second.

Flashing runs in a worker thread so the UI keeps redrawing; output lines are
pushed onto a queue the main loop drains. Nothing else is threaded.
"""
from __future__ import annotations

import curses
import queue
import sys
import threading
import time
from pathlib import Path

from . import (canbus, canflash, config, env, manifest as mf, params, sources,
               stlink)

HELP = ("j/k board  [/] scroll  b=boot  a=app  c=config  r=reset  m=monitor  "
        "f=firmware  d=doctor  x=CANCEL  q=quit")


class App:
    def __init__(self, firmware_dir, iface: str, bitrate: int):
        self.firmware_dir = self._resolve(
            Path(firmware_dir) if firmware_dir else mf.DEFAULT_FIRMWARE_DIR)
        self.manifest = self._load(self.firmware_dir)
        # Folder picker state. search_root is the directory scanned for
        # selectable firmware sets; by default the parent of the current one,
        # so sibling builds show up.
        self.search_root = self.firmware_dir.parent
        self.picking = False
        self.sources: list = []
        self.pick_sel = 0
        self.iface = iface
        self.bitrate = bitrate
        self.sel = 0
        self.log: list[tuple[str, str]] = []
        self.events: queue.Queue = queue.Queue()
        self.busy = False
        # The subprocess of a running flash, so it can be stopped. Selecting
        # the wrong board and starting a flash was previously unstoppable:
        # quitting killed the interpreter while BootCommander carried on
        # transmitting as an orphan.
        self.child = None
        # First visible telemetry line. Kept as an absolute index rather than a
        # frame number so it stays put as frames arrive and signals change -
        # a pane that re-anchored itself every 100 ms would be unreadable.
        self.tel_offset = 0
        self.monitor: canbus.Monitor | None = None
        self.bus = None
        self.monitoring = False
        # Config screen. Its bus is separate from the monitor's so leaving
        # config does not tear down a monitor session, and entering it does
        # not inherit a stream something else is already draining.
        self.config_mode = False
        self.config_db = None
        self.config_bus = None
        self.cfg_state: config.BoardState | None = None
        self.cfg_params: list = []
        self.cfg_sel = 0
        self.cfg_edits: dict = {}
        self.cfg_editing = False
        self.cfg_buf = ""
        self.cfg_timeout = config.DEFAULT_READ_TIMEOUT
        # Off by default and reset on every entry to the screen: consent to
        # switch a power rail should not outlive the moment it was given.
        self.cfg_allow_actuate = False
        self.cfg_confirm_defaults = False
        # An empty git_sha means the manifest was SYNTHESISED from a loose
        # folder of .srec files, not that a staged build had a dirty tree.
        # Both are untrusted, but saying "staged from a dirty tree" about a
        # folder that was never staged sends you looking for uncommitted
        # changes that do not exist. Reported on the bench as
        # "loaded manifest: git  DIRTY" - a blank sha and a DIRTY that came
        # from the synthesiser hard-coding git_dirty=True.
        if not self.manifest.git_sha:
            self.say("info", "loaded loose folder: no manifest, no provenance")
            self.say("warn", "images are unverified - no checksums to check "
                             "against and no build they can be traced to")
        else:
            self.say("info", f"loaded manifest: git {self.manifest.git_sha[:8]}"
                             f"{' DIRTY' if self.manifest.git_dirty else ''}")
            if self.manifest.git_dirty:
                self.say("warn", "images were staged from a dirty tree - "
                                 "provenance is not reproducible")

    # ------------------------------------------------------------ state --
    @staticmethod
    def _resolve(directory: Path) -> Path:
        """The version folder to open, given a version OR a container of them.

        firmware/ holds one folder per version, so both are things an operator
        will reasonably point at. Depth 0 first so naming a version selects
        exactly that and never a sibling; otherwise take the newest inside.
        """
        if sources.discover(directory, max_depth=0):
            return directory
        found = sources.discover(directory, max_depth=2)
        return found[0].path if found else directory

    @staticmethod
    def _load(directory: Path) -> mf.Manifest:
        """Load a folder, accepting a loose .srec drop as well as a staged set."""
        found = sources.discover(directory, max_depth=0)
        if found:
            return sources.load(found[0])
        return mf.load_manifest(directory)

    @property
    def board(self) -> mf.BoardImages:
        return self.manifest.boards[self.sel]

    # ----------------------------------------------------- folder picker --
    def open_picker(self) -> None:
        self.sources = sources.discover(self.search_root)
        self.pick_sel = 0
        if not self.sources:
            self.say("warn", f"no firmware folders under {self.search_root}")
            return
        self.picking = True

    def choose_source(self) -> None:
        src = self.sources[self.pick_sel]
        try:
            self.manifest = self._load(src.path)
        except mf.ManifestError as exc:
            self.say("err", f"{src.path.name}: {str(exc).splitlines()[0]}")
            return
        self.firmware_dir = src.path
        self.sel = 0
        self.picking = False
        self.say("ok", f"firmware: {src.path.name} "
                       f"({len(self.manifest.boards)} boards)")
        if not src.trusted:
            self.say("warn", "no manifest in that folder - images cannot be "
                             "checksum-verified against a source of truth")
        for n in src.notes:
            self.say("warn", n)

    def say(self, level: str, text: str) -> None:
        self.log.append((level, text))
        del self.log[:-400]

    # ----------------------------------------------------------- actions --
    def _worker(self, fn) -> None:
        """Run a blocking action off the UI thread, funnelling output to the queue."""
        self.busy = True
        self.cancelled = False

        def run():
            try:
                fn(lambda line: self.events.put(("out", line.rstrip())))
            except Exception as exc:                     # noqa: BLE001
                self.events.put(("err", f"{type(exc).__name__}: {exc}"))
            finally:
                self.child = None
                self.events.put(("done", ""))

        threading.Thread(target=run, daemon=True).start()

    def cancel(self) -> None:
        """Stop a running flash.

        Killing mid-write leaves a partial image, which is recoverable: only
        the application region is being written, the bootloader is untouched,
        and re-flashing fixes it. Continuing to write the WRONG image to a
        board is not recoverable in the same easy way, so stopping wins.
        """
        proc = self.child
        if proc is None:
            self.say("warn", "nothing running to cancel")
            return
        self.cancelled = True
        try:
            proc.kill()
            self.say("warn", "CANCELLED - the image on the board is now "
                             "incomplete; re-flash the correct one")
        except Exception as exc:                          # noqa: BLE001
            self.say("err", f"could not stop it: {exc}")

    def kill_child(self) -> None:
        """Stop any running subprocess. Called on the way out.

        Without this, quitting orphaned BootCommander: the worker is a daemon
        thread, so the interpreter exits, but the flasher is a separate OS
        process and kept polling - transmitting an XCP CONNECT ~17 times a
        second onto a bus other boards are using.
        """
        proc = self.child
        if proc is None:
            return
        try:
            proc.kill()
            proc.wait(timeout=2)
        except Exception:                                 # noqa: BLE001
            pass
        self.child = None

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
            on_output=cb, on_start=self._adopt))

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
            extended=board.extended, on_output=cb, on_start=self._adopt))

    def clamp_telemetry_offset(self, total: int, capacity: int) -> int:
        """Keep the window inside the list, and return the offset to draw at.

        Clamped at render time rather than on the keypress: the list grows and
        shrinks as frames appear, so an offset that was valid when the key was
        pressed may not be one frame later. Scrolling to the bottom of a short
        list and then losing a frame must not leave the pane blank.
        """
        self.tel_offset = max(0, min(self.tel_offset, max(0, total - capacity)))
        return self.tel_offset

    def scroll_telemetry(self, delta: int) -> None:
        self.tel_offset = max(0, self.tel_offset + delta)

    def _adopt(self, proc) -> None:
        """Remember the child so cancel() and kill_child() can reach it."""
        self.child = proc

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
            path = canbus.find_dbc(board.dbc, self.manifest.root)
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

    # ------------------------------------------------------------ config --
    def board_db(self):
        """The selected board's DBC, or None. Same resolution as the monitor."""
        board = self.board
        if not board.dbc:
            return None
        path = canbus.find_dbc(board.dbc, self.manifest.root)
        if not path:
            return None
        try:
            return canbus.load_dbc(path)
        except Exception as exc:                          # noqa: BLE001
            self.say("warn", f"DBC load failed: {exc}")
            return None

    def enter_config(self) -> None:
        board = self.board
        db = self.board_db()
        if db is None:
            self.say("err", f"no DBC for {board.id}: configuration is "
                            f"addressed by signal name, so there is nothing "
                            f"to read or write without one")
            return
        if not params.signals(board.id):
            self.say("err", f"no configurable parameters known for {board.id}")
            return
        # The monitor and a config read would compete for the same frames off
        # one bus and each would see a random half of them. Reading a board
        # back needs every frame, so the monitor stands down.
        if self.monitoring:
            self.toggle_monitor()
            self.say("info", "monitor stopped: config needs the whole stream")
        self.config_db = db
        self.config_mode = True
        self.cfg_sel = 0
        self.cfg_edits = {}
        self.cfg_editing = False
        self.cfg_buf = ""
        self.cfg_confirm_defaults = False
        self.cfg_params = [(g, p) for g in params.for_board(board.id).groups
                           for p in g.params]
        self.config_refresh()

    def leave_config(self) -> None:
        self.config_mode = False
        self.cfg_editing = False
        if self.cfg_edits:
            self.say("warn", f"{len(self.cfg_edits)} edited value(s) were "
                             f"never written")
        self.cfg_edits = {}
        if self.config_bus is not None:
            self.config_bus.shutdown()
            self.config_bus = None

    def _cfg_bus(self):
        if self.config_bus is None:
            self.config_bus = canbus.open_bus(self.iface)
        return self.config_bus

    def config_refresh(self) -> None:
        board = self.board

        def job(emit):
            state = config.collect(self._cfg_bus(), self.config_db, board.id,
                                   timeout=self.cfg_timeout)
            self.cfg_state = state
            if state.missing:
                emit(f"no frames seen for: {', '.join(state.missing)}")
                emit("those parameters read as unknown, not as zero")
            else:
                emit(f"read {len(state.values)} parameter(s) from {board.name}")

        self._worker(job)

    def cfg_current(self):
        if not self.cfg_params:
            return None, None
        return self.cfg_params[min(self.cfg_sel, len(self.cfg_params) - 1)]

    def cfg_begin_edit(self) -> None:
        _, param = self.cfg_current()
        if param is None:
            return
        self.cfg_editing = True
        self.cfg_buf = ""

    def cfg_commit_edit(self) -> None:
        _, param = self.cfg_current()
        self.cfg_editing = False
        if param is None or not self.cfg_buf:
            return
        try:
            value = int(self.cfg_buf, 0)
        except ValueError:
            try:
                value = float(self.cfg_buf)
            except ValueError:
                self.say("err", f"{self.cfg_buf!r} is not a number")
                self.cfg_buf = ""
                return
        self.cfg_edits[param.signal] = value
        self.cfg_buf = ""

    def cfg_type(self, ch: str) -> None:
        if ch in "0123456789.-xabcdefABCDEF":
            self.cfg_buf += ch

    def cfg_backspace(self) -> None:
        self.cfg_buf = self.cfg_buf[:-1]

    def config_write(self, all_edits: bool = False) -> None:
        """Write the selected parameter, or every edited one."""
        if not self.cfg_edits:
            self.say("warn", "nothing edited to write")
            return
        board = self.board
        if all_edits:
            wanted = dict(self.cfg_edits)
        else:
            _, param = self.cfg_current()
            if param is None or param.signal not in self.cfg_edits:
                self.say("warn", "this parameter has no edited value")
                return
            wanted = {param.signal: self.cfg_edits[param.signal]}

        actuating = [p for _, p in self.cfg_params
                     if p.signal in wanted and p.actuates]
        if actuating and not self.cfg_allow_actuate:
            for p in actuating:
                self.say("warn", f"{p.signal} {p.actuates}")
            self.say("warn", "press A to allow writing parameters that drive "
                             "hardware, then write again")
            return

        def job(emit):
            state = self.cfg_state
            for message, overrides in config.group_overrides(
                    board.id, wanted).items():
                results, state = config.write_and_verify(
                    self._cfg_bus(), self.config_db, board, message, overrides,
                    state=state, timeout=self.cfg_timeout)
                for r in results:
                    if r.signal not in overrides:
                        continue
                    emit(f"{'OK  ' if r.ok else 'FAIL'} {r.signal} "
                         f"wrote={r.wanted} live={r.live_status} "
                         f"stored={r.stored_status}")
                    if r.ok:
                        self.cfg_edits.pop(r.signal, None)
            self.cfg_state = state
            emit("written - not persistent until you press s (EEPROM save)")

        self._worker(job)

    def config_save(self) -> None:
        board = self.board

        def job(emit):
            arb_id, data = config.persist(self._cfg_bus(), self.config_db,
                                          board)
            emit(f"EEPROM save sent id=0x{arb_id:03X} data={data.hex(' ')}")
            self.cfg_state = config.collect(
                self._cfg_bus(), self.config_db, board.id,
                timeout=config.DEFAULT_PERSIST_TIMEOUT)
            emit("re-read after save")

        self._worker(job)

    def config_defaults(self) -> None:
        """Restore factory defaults. Confirmed, because it overwrites EEPROM."""
        board = self.board
        if not self.cfg_confirm_defaults:
            self.cfg_confirm_defaults = True
            self.say("warn", f"press D again to overwrite {board.name}'s "
                             f"stored configuration with factory defaults")
            return
        self.cfg_confirm_defaults = False

        def job(emit):
            arb_id, data = config.load_defaults(self._cfg_bus(),
                                                self.config_db, board)
            emit(f"load-defaults sent id=0x{arb_id:03X} data={data.hex(' ')}")
            self.cfg_state = config.collect(
                self._cfg_bus(), self.config_db, board.id,
                timeout=config.DEFAULT_PERSIST_TIMEOUT)
            emit("re-read after defaults")

        self._worker(job)

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


def _draw_picker(stdscr, app: App, h: int, w: int) -> None:
    """Full-window folder picker. Newest first, as sources.discover orders."""
    import datetime
    stdscr.addnstr(0, 0, f" SELECT FIRMWARE - {app.search_root} ".ljust(w - 1),
                   w - 1, curses.A_REVERSE)
    stdscr.addnstr(1, 0, "  newest first; Enter=select  Esc/f=cancel", w - 1,
                   curses.A_DIM)
    for i, s in enumerate(app.sources):
        row = 3 + i * 2
        if row + 1 >= h - 1:
            break
        attr = curses.A_REVERSE if i == app.pick_sel else curses.A_NORMAL
        when = datetime.datetime.fromtimestamp(s.mtime).strftime("%Y-%m-%d %H:%M")
        tag = "verified" if s.trusted else "UNVERIFIED"
        stdscr.addnstr(row, 0,
                       f" {when}  {s.path.name:<26} {s.srec_count} srec  {tag}",
                       w - 1, attr)
        detail = f"      {', '.join(s.boards) or '?'}"
        if s.config:
            detail += f"   config: {s.config.name}"
        if s.notes:
            detail += f"   ({s.notes[0]})"
        stdscr.addnstr(row + 1, 0, detail, w - 1,
                       curses.A_DIM if s.trusted else curses.color_pair(2))
    stdscr.refresh()


def _draw(stdscr, app: App) -> None:
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    if app.picking:
        _draw_picker(stdscr, app, h, w)
        return
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

    # right pane: the config table when that screen is open, else telemetry
    capacity = max(1, split - 1)

    def put_lines(lines, first):
        for i, (indent, text, dim) in enumerate(lines[first:first + capacity]):
            stdscr.addnstr(1 + i, left + indent, text,
                           max(1, w - left - indent - 1),
                           curses.A_DIM if dim else curses.A_NORMAL)

    if app.config_mode:
        title = " CONFIG " + app.board.name + " "
        if app.cfg_allow_actuate:
            title += "[ACTUATE ALLOWED] "
        stdscr.addnstr(0, left, title.ljust(w - left - 1), w - left - 1,
                       curses.A_REVERSE)
        lines = config_lines(app)
        # Keep the cursor on screen without a second scroll offset to manage:
        # the selected row is the only one that has to be visible.
        put_lines(lines, max(0, min(app.cfg_sel + 3 - capacity,
                                    len(lines) - capacity)))
        if app.cfg_state is None:
            stdscr.addnstr(1, left + 1, "reading board...", w - left - 2,
                           curses.A_DIM)
    elif app.monitoring and app.monitor:
        stdscr.addnstr(0, left, " TELEMETRY ".ljust(w - left - 1), w - left - 1,
                       curses.A_REVERSE)   # overwritten below when it scrolls
        frames = app.monitor.snapshot()
        # Render a WINDOW onto every line rather than rationing rows between
        # frames. PowerStage needs about sixty rows for ten broadcasts of up to
        # eight signals; the pane has under thirty. Any fixed budget therefore
        # hides real data - the previous one said "+N more", which named what
        # you could not see without letting you see it.
        lines = telemetry_lines(app)
        offset = app.clamp_telemetry_offset(len(lines), capacity)

        header = telemetry_header(offset, capacity, len(lines))
        if header:
            stdscr.addnstr(0, left, header.ljust(w - left - 1),
                           w - left - 1, curses.A_REVERSE)

        put_lines(lines, offset)
        if not frames:
            stdscr.addnstr(1, left + 1, "waiting for traffic...", w - left - 2,
                           curses.A_DIM)
    else:
        stdscr.addnstr(0, left, " TELEMETRY ".ljust(w - left - 1), w - left - 1,
                       curses.A_REVERSE)
        stdscr.addnstr(1, left + 1, "monitor stopped - press m", w - left - 2,
                       curses.A_DIM)

    # log
    stdscr.addnstr(split, 0, " LOG ".ljust(w - 1), w - 1, curses.A_REVERSE)
    visible = app.log[-(h - split - 2):]
    for i, (level, text) in enumerate(visible):
        stdscr.addnstr(split + 1 + i, 0, text[:w - 1], w - 1, _colour(level))

    man = app.manifest
    # Same distinction as the startup banner: no sha means a synthesised
    # manifest (loose folder), not a dirty build tree. Rendering "git  DIRTY"
    # with an empty sha is how this looked on the bench, and it reads as a
    # staged build gone wrong rather than an unstaged folder.
    if man.git_sha:
        prov = f"git {man.git_sha[:8]}{' DIRTY' if man.git_dirty else ''}"
    else:
        prov = "UNVERIFIED"
    help_text = CONFIG_HELP if app.config_mode else HELP
    status = (f" {app.iface} | {app.firmware_dir.name} | {prov} | "
              f"{'BUSY' if app.busy else 'idle'} | {help_text}")
    stdscr.addnstr(h - 1, 0, status.ljust(w - 1)[:w - 1], w - 1, curses.A_REVERSE)
    stdscr.refresh()


def telemetry_header(offset: int, capacity: int, total: int) -> str:
    """Pane title showing the visible range, or "" when everything fits.

    Separate and pure so the position arithmetic can be tested without a
    terminal - screen-scraping a curses redraw over ssh proved an unreliable
    way to check an off-by-one.
    """
    if total <= capacity:
        return ""
    last = min(offset + capacity, total)
    return f" TELEMETRY  {offset + 1}-{last}/{total}  [ ] PgUp/PgDn scroll "


def telemetry_lines(app) -> list[tuple[int, str, bool]]:
    """Every renderable line for the current snapshot, as (indent, text, dim).

    Built in full and windowed by the caller, so nothing is ever dropped for
    lack of space - a frame's ninth signal is one PgDn away rather than gone.
    Pure apart from reading the monitor, which is what makes it testable
    without curses.
    """
    if not app.monitor:
        return []
    counts = app.monitor.counts()
    lines: list[tuple[int, str, bool]] = []
    for f in app.monitor.snapshot():
        rate = app.monitor.rate_hz(f.arb_id)
        lines.append((
            1,
            f"0x{f.arb_id:03X} {(f.name or 'raw'):<20} "
            f"n={counts.get(f.arb_id, 0):<4} "
            f"{(f'{rate:4.1f}Hz' if rate else '   -  ')} "
            f"{f.raw.hex(' ')}",
            False))
        for k, v in f.signals.items():
            lines.append((3, f"{k} = {v}", True))
    return lines


def _fmt_value(value) -> str:
    if value is None:
        return "-"
    if isinstance(value, float) and value == int(value):
        return str(int(value))
    return str(value)


def config_lines(app) -> list[tuple[int, str, bool]]:
    """Every renderable line of the config table, as (indent, text, dim).

    Three columns because there are three facts and conflating any two of them
    misleads: what you have typed but not sent, what the board is doing, and
    what it will come back to after a power cycle. Pure apart from reading
    app state, so it is testable without curses - which matters here, because
    this file has never been executed on the machine it was written on.
    """
    state = app.cfg_state
    lines: list[tuple[int, str, bool]] = []
    header = f"{'parameter':26} {'desired':>10} {'live':>10} {'stored':>10}"
    lines.append((1, header, True))

    last_group = None
    for index, (grp, param) in enumerate(app.cfg_params):
        if grp is not last_group:
            last_group = grp
            lines.append((1, f"{grp.message}", False))
            if grp.note:
                lines.append((3, grp.note, True))

        value = state.get(param.signal) if state else None
        live = _fmt_value(value.live) if value and value.live_seen else "-"
        stored = _fmt_value(value.stored) if value and value.stored_seen else "-"

        if app.cfg_editing and index == app.cfg_sel:
            desired = (app.cfg_buf or "") + "_"
        elif param.signal in app.cfg_edits:
            desired = "*" + _fmt_value(app.cfg_edits[param.signal])
        else:
            desired = "-"

        cursor = ">" if index == app.cfg_sel else " "
        flag = " !" if param.actuates else ""
        lines.append((
            2,
            f"{cursor}{param.signal:25} {desired:>10} {live:>10} "
            f"{stored:>10}{flag}",
            False))
    return lines


#: Shown in the status bar while the config screen is open.
CONFIG_HELP = ("j/k param  e=edit  w=write  W=write-all  s=SAVE-eeprom  "
               "D=defaults  R=re-read  A=allow-actuate  c/Esc=back")


def _handle_config_key(app, ch: int, key: str) -> None:
    """Key handling for the config screen. Separated so it can be tested."""
    if app.cfg_editing:
        if ch in (27,):                                   # Esc abandons
            app.cfg_editing = False
            app.cfg_buf = ""
        elif ch in (curses.KEY_ENTER, 10, 13):
            app.cfg_commit_edit()
        elif ch in (curses.KEY_BACKSPACE, 127, 8):
            app.cfg_backspace()
        elif key:
            app.cfg_type(key)
        return

    if ch == 27 or key == "c":
        app.leave_config()
    elif ch == curses.KEY_DOWN or key == "j":
        app.cfg_sel = (app.cfg_sel + 1) % max(1, len(app.cfg_params))
    elif ch == curses.KEY_UP or key == "k":
        app.cfg_sel = (app.cfg_sel - 1) % max(1, len(app.cfg_params))
    elif app.busy:
        app.say("warn", "a command is already running")
    elif key in ("e",) or ch in (curses.KEY_ENTER, 10, 13):
        app.cfg_begin_edit()
    elif key == "w":
        app.config_write()
    elif key == "W":
        app.config_write(all_edits=True)
    elif key == "s":
        app.config_save()
    elif key == "D":
        app.config_defaults()
    elif key == "R":
        app.config_refresh()
    elif key == "A":
        app.cfg_allow_actuate = not app.cfg_allow_actuate
        app.say("warn" if app.cfg_allow_actuate else "info",
                f"writing parameters that drive hardware is "
                f"{'ALLOWED' if app.cfg_allow_actuate else 'blocked'}")


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

        if app.picking:
            if ch in (27,) or key == "f":            # Esc or f cancels
                app.picking = False
            elif ch in (curses.KEY_DOWN,) or key == "j":
                app.pick_sel = (app.pick_sel + 1) % len(app.sources)
            elif ch in (curses.KEY_UP,) or key == "k":
                app.pick_sel = (app.pick_sel - 1) % len(app.sources)
            elif ch in (curses.KEY_ENTER, 10, 13):
                app.choose_source()
            continue

        if key in ("q", "Q") and not (app.config_mode and app.cfg_editing):
            # Kill first, then close the bus: leaving a flasher running is the
            # one thing that outlives this process and corrupts the next
            # session's measurements.
            app.kill_child()
            if app.bus:
                app.bus.shutdown()
            if app.config_bus:
                app.config_bus.shutdown()
            return 0

        if app.config_mode:
            _handle_config_key(app, ch, key)
            continue
        if key in ("x", "X") or ch == 3:            # x or Ctrl-C
            app.cancel()
            continue
        # Telemetry scrolling, handled before the busy guard below: a flash is
        # exactly when you want to watch the bus, and the guard swallows keys.
        if key == "]" or ch == curses.KEY_NPAGE:
            app.scroll_telemetry(5)
            continue
        if key == "[" or ch == curses.KEY_PPAGE:
            app.scroll_telemetry(-5)
            continue
        if ch == curses.KEY_HOME or key == "0":
            app.tel_offset = 0
            continue

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
        elif key == "c":
            app.enter_config()
        elif key == "f":
            app.open_picker()


def run_tui(firmware_dir: Path | str | None, iface: str, bitrate: int) -> int:
    # curses needs a real terminal on stdin. Without this check, running the
    # TUI over a pipe or a non-interactive ssh dies inside curses.wrapper with
    #     _curses.error: cbreak() returned ERR
    # and then the cleanup path raises `nocbreak() returned ERR` on top, so the
    # traceback the operator sees names neither the cause nor the fix. Every
    # other subcommand works fine without a tty, which makes the TUI's failure
    # look like a bug in the tool rather than a missing `-t`.
    if not (sys.stdin.isatty() and sys.stdout.isatty()):
        print("the TUI needs an interactive terminal, and stdin/stdout is not "
              "one.\n"
              "  over ssh:   ssh -t <host> '<command>'\n"
              "  in scripts: use the plain subcommands instead - flash, "
              "verify, monitor, doctor,\n"
              "              reset and release all work without a terminal.",
              file=sys.stderr)
        return 2

    app = App(firmware_dir, iface, bitrate)
    return curses.wrapper(_loop, app)
