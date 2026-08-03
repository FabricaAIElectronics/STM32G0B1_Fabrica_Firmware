"""Tests for the HIL verification checks.

A fake board runs on python-can's virtual backend and answers like the real
firmware. The failure modes matter as much as the pass: a check that has never
seen a silent board, or a board whose telemetry ignores commands, proves
nothing when it goes green on the bench.
"""
from __future__ import annotations

import sys
import threading
import time
from pathlib import Path

import can
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fabrica import canbus, verify  # noqa: E402
from fabrica.canflash import RESET_PAYLOAD  # noqa: E402

CHANNEL = "verifytest"


class FakeBoard(threading.Thread):
    """A board that broadcasts periodically and optionally obeys commands."""

    def __init__(self, channel, broadcast_ids, *, period=0.05, obey=True,
                 cmd_id=0x140, bcast_id=0x153, stop_on_reset=True):
        super().__init__(daemon=True)
        self.bus = can.Bus(channel=channel, interface="virtual")
        self.broadcast_ids = list(broadcast_ids)
        self.period = period
        self.obey = obey
        self.cmd_id = cmd_id
        self.bcast_id = bcast_id
        self.stop_on_reset = stop_on_reset
        self.duty = 0
        self.running = True
        self.in_bootloader = False

    def run(self):
        nxt = time.time()
        while self.running:
            msg = self.bus.recv(timeout=0.01)
            if msg is not None:
                data = bytes(msg.data)
                if msg.arbitration_id == 0x130 and data[:2] == b"\xff\x00":
                    if self.stop_on_reset:
                        self.in_bootloader = True
                elif msg.arbitration_id == self.cmd_id and self.obey:
                    self.duty = data[1] if len(data) > 1 else 0
            if self.in_bootloader:
                continue
            if time.time() >= nxt:
                nxt = time.time() + self.period
                for bid in self.broadcast_ids:
                    payload = bytes([self.duty] * 4) if bid == self.bcast_id \
                        else bytes(4)
                    self.bus.send(can.Message(arbitration_id=bid, data=payload,
                                              is_extended_id=False))

    def stop(self):
        self.running = False
        self.join(timeout=1.0)
        self.bus.shutdown()


class RebootingBus:
    """A deterministic stand-in for a bus attached to a rebooting board.

    Not threaded. Two threads racing over a virtual bus made this scenario
    flaky, and the thing under test is purely the timing arithmetic, so the
    fake drives the clock instead of a second board.

    Behaviour matches the real knob measured on the bench: broadcasts every
    `period`, and after FF 00 it goes quiet for `outage` before resuming. On
    hardware those were 0.100 s and 1.00 s.
    """

    def __init__(self, period=0.05, outage=0.6, reset_id=0x130):
        self.period = period
        self.outage = outage
        self.reset_id = reset_id
        self.quiet_until = 0.0
        self.sent = []

    def recv(self, timeout=0.0):
        now = time.time()
        wait = max(0.0, self.quiet_until - now)
        if wait:                              # rebooting: stay silent
            if wait >= timeout:
                time.sleep(timeout)
                return None
            time.sleep(wait)
        time.sleep(min(self.period, max(0.0, timeout)))
        return can.Message(arbitration_id=0x150, data=bytes(4),
                           is_extended_id=False, timestamp=time.time())

    def send(self, msg):
        self.sent.append(msg)
        if msg.arbitration_id == self.reset_id and \
                bytes(msg.data)[:2] == RESET_PAYLOAD:
            self.quiet_until = time.time() + self.outage

    def shutdown(self):
        pass


class Board:
    """Minimal stand-in for manifest.BoardImages."""
    id = "powerstage"
    blt_rx = 0x130
    blt_tx = 0x131


@pytest.fixture
def channel(request):
    return f"{CHANNEL}-{request.node.name}"


@pytest.fixture
def host(channel):
    bus = can.Bus(channel=channel, interface="virtual")
    yield bus
    bus.shutdown()


# ----------------------------------------------------- property 1: unsolicited
def test_unsolicited_passes_when_the_board_broadcasts(host, channel):
    board = FakeBoard(channel, [0x150, 0x151, 0x153])
    board.start()
    try:
        r = verify.check_unsolicited(host, Board(), None,
                                     {0x150, 0x151, 0x153}, seconds=0.6)
    finally:
        board.stop()
    assert r.status == verify.PASS, r.detail


def test_unsolicited_fails_on_a_silent_bus(host):
    r = verify.check_unsolicited(host, Board(), None, {0x150}, seconds=0.3)
    assert r.status == verify.FAIL
    assert "no CAN traffic at all" in r.detail


def test_unsolicited_names_the_missing_broadcast(host, channel):
    """A board that sends most of its telemetry but drops one id."""
    board = FakeBoard(channel, [0x150, 0x151])
    board.start()
    try:
        r = verify.check_unsolicited(host, Board(), None,
                                     {0x150, 0x151, 0x153}, seconds=0.6)
    finally:
        board.stop()
    assert r.status == verify.FAIL
    assert "0x153" in r.detail


# --------------------------------------------------------- property 2: causal
def test_command_changes_telemetry(host, channel):
    board = FakeBoard(channel, [0x153], obey=True)
    board.start()
    try:
        r = verify.check_command_changes_telemetry(host, Board(), None,
                                                   timeout=2.0)
    finally:
        board.stop()
    assert r.status == verify.PASS, r.detail
    assert "30->30" in r.detail and "70->70" in r.detail


def test_telemetry_that_ignores_the_command_is_caught(host, channel):
    """The failure that matters: the frame arrives but never reflects the command."""
    board = FakeBoard(channel, [0x153], obey=False)
    board.start()
    try:
        r = verify.check_command_changes_telemetry(host, Board(), None,
                                                   timeout=1.0)
    finally:
        board.stop()
    assert r.status == verify.FAIL
    assert "did not change" in r.detail


def test_missing_response_broadcast_is_caught(host, channel):
    board = FakeBoard(channel, [0x150])          # never sends 0x153
    board.start()
    try:
        r = verify.check_command_changes_telemetry(host, Board(), None,
                                                   timeout=0.6)
    finally:
        board.stop()
    assert r.status == verify.FAIL
    assert "never arrived" in r.detail


def test_board_without_a_stimulus_is_skipped(host):
    class Knob(Board):
        id = "knob"
    r = verify.check_command_changes_telemetry(host, Knob(), None)
    assert r.status == verify.SKIP


# ---------------------------------------------------------- property 3: reset
def test_reset_detected_when_the_board_stays_in_the_bootloader(host, channel):
    board = FakeBoard(channel, [0x150, 0x153], stop_on_reset=True)
    board.start()
    try:
        r = verify.check_reset_enters_bootloader(host, Board(), quiet_for=1.5)
    finally:
        board.stop()
    assert r.status == verify.PASS, r.detail
    assert board.in_bootloader


def test_reset_detected_when_the_application_comes_back(host, channel):
    """The case that actually happens on hardware.

    An earlier version of this check asserted the application stopped and
    STAYED stopped, and reported FAIL on a board that was resetting perfectly -
    because OpenBLT restarts the application once its backdoor window closes.
    The observable is the pause, not permanent silence.
    """
    bus = RebootingBus(period=0.05, outage=0.6)
    r = verify.check_reset_enters_bootloader(bus, Board(), quiet_for=2.0)
    assert r.status == verify.PASS, r.detail
    assert "paused" in r.detail
    # the trigger really was sent, and it was the documented one
    assert any(m.arbitration_id == 0x130 and bytes(m.data)[:2] == RESET_PAYLOAD
               for m in bus.sent)


def test_a_board_that_ignores_the_reset_is_caught(host, channel):
    """No pause at all means the trigger did nothing."""
    board = FakeBoard(channel, [0x150, 0x153], stop_on_reset=False)
    board.start()
    try:
        r = verify.check_reset_enters_bootloader(host, Board(), quiet_for=2.0)
    finally:
        board.stop()
    assert r.status == verify.FAIL
    assert "no restart seen" in r.detail


def test_reset_skipped_when_the_board_was_already_silent(host):
    r = verify.check_reset_enters_bootloader(host, Board(), quiet_for=0.3)
    assert r.status == verify.SKIP
    assert "not broadcasting" in r.detail


# ------------------------------------------------------------------ plumbing
def test_expected_ids_come_from_dbc_senders():
    dbc = canbus.find_dbc("PowerStage.dbc")
    db = canbus.load_dbc(dbc)
    ids = verify.expected_broadcast_ids(db, blt_tx=0x131)
    assert 0x150 in ids and 0x15A in ids       # board telemetry
    assert 0x140 not in ids                    # a command, host-sent
    assert 0x131 not in ids                    # bootloader TX, not the app


def test_reset_is_not_run_unless_asked(host):
    results = verify.run_all(host, Board(), None, set(), seconds=0.2)
    reset = [r for r in results if r.name == "reset"][0]
    assert reset.status == verify.SKIP
    assert "--include-reset" in reset.detail
