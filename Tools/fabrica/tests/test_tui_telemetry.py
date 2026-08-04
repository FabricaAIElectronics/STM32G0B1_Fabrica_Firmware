"""Telemetry pane windowing: pure logic, no curses."""
from __future__ import annotations

import sys
from pathlib import Path

_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from fabrica import tui  # noqa: E402
from fabrica.canbus import DecodedFrame  # noqa: E402


class FakeMonitor:
    def __init__(self, frames):
        self._frames = frames

    def snapshot(self):
        return self._frames

    def counts(self):
        return {f.arb_id: 1 for f in self._frames}

    def rate_hz(self, arb_id):
        return 10.0


class FakeApp:
    """Only what telemetry_lines and the offset maths touch."""

    def __init__(self, frames):
        self.monitor = FakeMonitor(frames)
        self.tel_offset = 0

    clamp_telemetry_offset = tui.App.clamp_telemetry_offset
    scroll_telemetry = tui.App.scroll_telemetry


def frame(arb_id, name, signals):
    return DecodedFrame(arb_id=arb_id, name=name, raw=bytes(8),
                        signals=signals, timestamp=0.0)


KNOBSTATE = frame(0x661, "KNOBSTATE", {f"Sig{i}": i for i in range(9)})
POWERSTAGE = [frame(0x150 + i, f"BCAST_{i}", {f"S{j}": j for j in range(8)})
              for i in range(10)]


def test_every_signal_gets_a_line():
    """Nothing is dropped for lack of space - that is what scrolling is for."""
    lines = tui.telemetry_lines(FakeApp([KNOBSTATE]))
    assert len(lines) == 1 + 9          # header + nine signals
    assert "KNOBSTATE" in lines[0][1]
    assert lines[-1][1] == "Sig8 = 8"


def test_powerstage_produces_more_lines_than_a_pane_can_hold():
    """The case that motivated scrolling: ten frames, eight signals each."""
    lines = tui.telemetry_lines(FakeApp(POWERSTAGE))
    assert len(lines) == 10 * (1 + 8)   # 90 lines
    assert len(lines) > 28              # a 40-row terminal has ~28 rows here


def test_signals_are_indented_under_their_frame():
    lines = tui.telemetry_lines(FakeApp([KNOBSTATE]))
    assert lines[0][0] < lines[1][0]


def test_offset_is_clamped_to_the_last_full_window():
    app = FakeApp(POWERSTAGE)
    total, capacity = 90, 28
    app.tel_offset = 500
    assert app.clamp_telemetry_offset(total, capacity) == total - capacity


def test_offset_never_goes_negative():
    app = FakeApp(POWERSTAGE)
    app.scroll_telemetry(-100)
    assert app.clamp_telemetry_offset(90, 28) == 0


def test_a_shrinking_list_pulls_the_window_back():
    """Frames come and go. Scrolled to the bottom of a long list and then
    losing frames must not leave the pane blank."""
    app = FakeApp(POWERSTAGE)
    app.scroll_telemetry(500)                       # page to the bottom
    app.tel_offset = app.clamp_telemetry_offset(90, 28)
    assert app.tel_offset == 90 - 28
    # A board drops off the bus; far fewer lines now.
    assert app.clamp_telemetry_offset(10, 28) == 0


def test_short_list_never_scrolls():
    app = FakeApp([KNOBSTATE])
    app.scroll_telemetry(5)
    assert app.clamp_telemetry_offset(10, 28) == 0


def test_no_monitor_yields_no_lines():
    app = FakeApp([])
    app.monitor = None
    assert tui.telemetry_lines(app) == []


# --- pane header -----------------------------------------------------------

def test_header_is_empty_when_everything_fits():
    """No scroll furniture on a pane that is not scrolling."""
    assert tui.telemetry_header(0, 28, 15) == ""
    assert tui.telemetry_header(0, 15, 15) == ""


def test_header_shows_the_visible_range_and_total():
    h = tui.telemetry_header(0, 28, 90)
    assert "1-28/90" in h
    assert "scroll" in h


def test_header_range_follows_the_offset():
    assert "29-56/90" in tui.telemetry_header(28, 28, 90)


def test_header_last_page_does_not_overshoot_the_total():
    """The window at the bottom is short; it must not claim lines that do not
    exist."""
    assert "63-90/90" in tui.telemetry_header(62, 28, 90)


def test_knob_on_a_short_terminal_scrolls():
    """A 16-row terminal leaves capacity 3, and the knob produces 18 lines."""
    lines = tui.telemetry_lines(FakeApp([KNOBSTATE] * 2))
    assert tui.telemetry_header(0, 3, len(lines)) != ""
