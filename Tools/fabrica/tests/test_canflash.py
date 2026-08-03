"""Unit tests for fabrica.canflash.

No hardware, no CAN bus, no BootCommander binary, no real subprocess. Every
test that executes anything injects a recording runner and asserts on the argv
that would have been executed. A module-level autouse fixture poisons
``subprocess.Popen`` inside canflash so that a test which accidentally reaches
the real default runner fails loudly instead of trying to talk to a bus.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Tools/fabrica/tests/test_canflash.py -> Tools/fabrica (the package root).
_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from fabrica import canflash  # noqa: E402
from fabrica.canflash import (  # noqa: E402
    EXTENDED_ID_FLAG,
    RESET_PAYLOAD,
    FlashResult,
    build_command,
    build_reset_frame,
    flash,
    parse_progress,
)

# A realistic PowerStage flash: can0 @ 500 kbps, bootloader RX 0x130 / TX 0x131
# (matches the powerStage preset in the team's flash_can.cfg).
BC = "/opt/openblt/Host/BootCommander"
IFACE = "can0"
BITRATE = 500000
PS_TID = 0x130
PS_RID = 0x131
SREC = Path("/opt/fabrica/firmware/powerstage/powerstage_app.srec")


@pytest.fixture(autouse=True)
def no_real_subprocess(monkeypatch):
    """Any attempt to spawn a real process is a test bug."""

    def _boom(*args, **kwargs):
        raise AssertionError(f"a test tried to spawn a real process: {args!r}")

    monkeypatch.setattr(canflash.subprocess, "Popen", _boom)


class RecordingRunner:
    """Injectable execution seam. Records calls, returns a canned result."""

    def __init__(self, returncode: int = 0, output: str = "", emit: list[str] | None = None):
        self.returncode = returncode
        self.output = output
        self.emit = emit or []
        self.calls: list[list[str]] = []

    def __call__(self, cmd, on_output=None):
        self.calls.append(list(cmd))
        for line in self.emit:
            if on_output is not None:
                on_output(line)
        return self.returncode, self.output

    @property
    def called(self) -> bool:
        return bool(self.calls)


# ---------------------------------------------------------------- argv ----

def test_build_command_exact_argv_for_powerstage():
    """The full argv for a realistic PowerStage flash, element by element."""
    assert build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC) == [
        "/opt/openblt/Host/BootCommander",
        "-s=xcp",
        "-t=xcp_can",
        "-d=can0",
        "-b=500000",
        "-tid=130",
        "-rid=131",
        str(SREC),
    ]


def test_srec_is_the_last_argument():
    cmd = build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC)
    assert cmd[-1] == str(SREC)
    assert cmd[-1].endswith("powerstage_app.srec")


def test_ids_are_uppercase_hex_without_0x_prefix():
    """Chosen convention: UPPERCASE hex, no '0x'. Same as flash_can.sh."""
    cmd = build_command(BC, IFACE, BITRATE, 0x1AB, 0x0CD, SREC)
    assert "-tid=1AB" in cmd
    assert "-rid=CD" in cmd
    for arg in cmd:
        assert "0x" not in arg
        assert "1ab" not in arg and "cd" not in arg  # never lower-case


def test_ids_are_not_zero_padded():
    cmd = build_command(BC, IFACE, BITRATE, 0x1, 0x22, SREC)
    assert "-tid=1" in cmd
    assert "-rid=22" in cmd


def test_bitrate_and_timeout_are_configurable():
    cmd = build_command(BC, "can1", 1000000, PS_TID, PS_RID, SREC, timeout_ms=2500)
    assert "-d=can1" in cmd
    assert "-b=1000000" in cmd
    assert "-t1=2500" in cmd


def test_t1_is_omitted_by_default():
    """flash_can.sh does not pass -t1, and 1000 ms is BootCommander's default.

    The default argv must be byte-for-byte the invocation already proven on the
    bench, so an unrecognised option cannot be the thing that breaks Monday.
    """
    assert not any(a.startswith("-t1=")
                   for a in build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC))


def test_t1_is_emitted_when_asked_for():
    assert "-t1=2500" in build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC,
                                       timeout_ms=2500)


# ------------------------------------------------------------ extended ----

def test_extended_uses_the_xid_flag_and_leaves_ids_bare():
    """29-bit addressing is signalled by -xid=1, not by ORing 0x80000000.

    0x80000000 is LibOpenBLT's internal CAN_MSG_EXT_ID_MASK at the driver layer,
    not the CLI convention. flash_can.sh:285 appends -xid=1 and leaves -tid/-rid
    bare; this matches the script that already works on the bench.
    """
    cmd = build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC, extended=True)
    assert "-tid=130" in cmd
    assert "-rid=131" in cmd
    assert "-xid=1" in cmd
    assert not any(a.startswith("-tid=8") for a in cmd)


def test_xid_absent_for_standard_ids():
    cmd = build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC)
    assert "-xid=1" not in cmd


def test_extended_false_leaves_ids_bare():
    cmd = build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC, extended=False)
    assert "-tid=130" in cmd
    assert "-rid=131" in cmd
    assert not any(arg.startswith("-tid=8") for arg in cmd)


def test_extended_allows_29bit_ids():
    cmd = build_command(BC, IFACE, BITRATE, 0x18DAF110, 0x18DA10F1, SREC, extended=True)
    assert "-tid=18DAF110" in cmd
    assert "-rid=18DA10F1" in cmd
    assert "-xid=1" in cmd


def test_standard_id_above_11_bits_is_rejected():
    with pytest.raises(ValueError, match="11-bit"):
        build_command(BC, IFACE, BITRATE, 0x800, PS_RID, SREC)


def test_extended_id_above_29_bits_is_rejected():
    with pytest.raises(ValueError, match="29-bit"):
        build_command(BC, IFACE, BITRATE, 0x20000000, PS_RID, SREC, extended=True)


# ------------------------------------------------------------- dry run ----

def test_dry_run_executes_nothing_and_reports_ok():
    runner = RecordingRunner(returncode=99, output="should never be used")
    result = flash(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC,
                   dry_run=True, runner=runner)

    assert runner.called is False          # provably executed nothing
    assert runner.calls == []
    assert result.ok is True
    assert result.returncode == 0
    assert result.command == build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC)
    assert "DRY RUN" in result.output
    assert "-tid=130" in result.output


def test_dry_run_reports_the_command_through_on_output():
    seen: list[str] = []
    runner = RecordingRunner()
    flash(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC,
          dry_run=True, on_output=seen.append, runner=runner)
    assert runner.called is False
    assert len(seen) == 1
    assert seen[0].startswith("DRY RUN")


# --------------------------------------------------------------- flash ----

def test_success_gives_ok_true_and_the_command_used():
    runner = RecordingRunner(returncode=0, output="Connecting to target...OK\n")
    result = flash(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC, runner=runner)

    assert isinstance(result, FlashResult)
    assert result.ok is True
    assert result.returncode == 0
    assert result.output == "Connecting to target...OK\n"
    assert runner.calls == [build_command(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC)]
    assert result.command == runner.calls[0]


def test_nonzero_returncode_gives_ok_false_with_output_captured():
    failure = (
        "Connecting to target...\n"
        "ERROR: could not connect to the target\n"
    )
    runner = RecordingRunner(returncode=1, output=failure)
    result = flash(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC, runner=runner)

    assert result.ok is False
    assert result.returncode == 1
    assert result.output == failure
    assert "could not connect" in result.output
    assert result.command[0] == BC


def test_flash_forwards_extended_and_timeout_to_the_command():
    runner = RecordingRunner()
    result = flash(BC, "can1", 250000, PS_TID, PS_RID, SREC,
                   extended=True, timeout_ms=3000, runner=runner)
    assert runner.calls[0] == result.command
    assert "-tid=130" in result.command
    assert "-xid=1" in result.command
    assert "-t1=3000" in result.command
    assert "-b=250000" in result.command


def test_on_output_callback_is_passed_to_the_runner():
    seen: list[str] = []
    runner = RecordingRunner(emit=["Erasing...  50%", "Programming...  100%"])
    flash(BC, IFACE, BITRATE, PS_TID, PS_RID, SREC,
          on_output=seen.append, runner=runner)
    assert seen == ["Erasing...  50%", "Programming...  100%"]
    assert [parse_progress(line) for line in seen] == [50, 100]


def test_missing_bootcommander_raises_with_the_candidates_listed(monkeypatch):
    monkeypatch.setattr(canflash, "find_bootcommander", lambda: None)
    with pytest.raises(FileNotFoundError, match="BootCommander not found"):
        build_command("", IFACE, BITRATE, PS_TID, PS_RID, SREC)


def test_blank_bootcommander_falls_back_to_discovery(monkeypatch):
    monkeypatch.setattr(canflash, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    cmd = build_command("", IFACE, BITRATE, PS_TID, PS_RID, SREC)
    assert cmd[0] == "/usr/local/bin/BootCommander"


# --------------------------------------------------------- reset frame ----

def test_build_reset_frame_powerstage():
    assert build_reset_frame(0x130) == (0x130, b"\xff\x00")


@pytest.mark.parametrize("blt_rx", [0x101, 0x130, 0x160, 0x7FF, 0x18DAF110])
def test_build_reset_frame_payload_is_always_ff00_dlc2(blt_rx):
    arb_id, payload = build_reset_frame(blt_rx)
    assert arb_id == blt_rx
    assert payload == RESET_PAYLOAD == b"\xff\x00"
    assert len(payload) == 2


def test_build_reset_frame_rejects_impossible_ids():
    with pytest.raises(ValueError):
        build_reset_frame(-1)
    with pytest.raises(ValueError):
        build_reset_frame(0x20000000)


# ------------------------------------------------------------ progress ----

@pytest.mark.parametrize("line,expected", [
    ("0%", 0),
    ("100%", 100),
    ("  45%", 45),
    ("Erasing 16384 bytes starting at 0x08008000...  25%", 25),
    ("Programming data... 100%", 100),
    ("Progress:7%", 7),
    ("  ###   62%   ###", 62),
    ("Programming 8192 bytes... 99 %", 99),
    ("Erasing...  99.5%", 99),          # fractional truncates
    ("Erasing... 20%\rErasing... 40%", 40),   # last update in the chunk wins
])
def test_parse_progress_extracts_percentage(line, expected):
    assert parse_progress(line) == expected


@pytest.mark.parametrize("line", [
    "",
    "Connecting to target...OK",
    "Loading program data from powerstage_app.srec...OK",
    "Erasing 16384 bytes starting at 0x08008000",
    "ERROR: could not connect to the target",
    "Success! Firmware update completed.",
    "100 percent complete",
    "Disconnecting from target",
    "%",                       # a bare percent sign is not progress
    "Bus load spiked to 250%",  # out of range is not progress
])
def test_parse_progress_returns_none_without_a_percentage(line):
    assert parse_progress(line) is None


# --- flash timeout ---------------------------------------------------------

class _NeverEndingProc:
    """A BootCommander that polls forever, like the real one does.

    Stands in for a flash aimed at a board that is not on the bus. Uses the
    module's fake-process seam rather than spawning anything, so the autouse
    no_real_subprocess guard above stays honest.
    """

    def __init__(self):
        self.killed = False
        self.stdout = self

    def readline(self):
        return "  Connecting to target bootloader...\n"

    def close(self):
        pass

    def kill(self):
        self.killed = True

    def wait(self):
        return -9


def test_run_subprocess_kills_a_flash_that_never_finishes(monkeypatch):
    """BootCommander polls for the bootloader forever, transmitting each time.

    A leftover attempt aimed at a board that was not on the bus ran for five
    minutes at ~17 frames/s and made an unrelated knob miss its 0x667 reset
    trigger, which verify then reported as "the board appears to have ignored
    FF 00" against firmware that was fine. The process must not outlive the
    command that started it.
    """
    proc = _NeverEndingProc()
    monkeypatch.setattr(canflash.subprocess, "Popen", lambda *a, **k: proc)

    rc, out = canflash.run_subprocess(["BootCommander", "..."], timeout_s=0.05)

    assert rc == 124                      # timeout(1)'s convention
    assert proc.killed, "the poller must be killed, not just abandoned"
    assert "timed out" in out
    assert "corrupt later measurements" in out


def test_a_flash_that_finishes_is_not_killed(monkeypatch):
    class _Finishes(_NeverEndingProc):
        def __init__(self):
            super().__init__()
            self._lines = iter(["  Programming...\n", "  OK\n", ""])

        def readline(self):
            return next(self._lines)

        def wait(self):
            return 0

    proc = _Finishes()
    monkeypatch.setattr(canflash.subprocess, "Popen", lambda *a, **k: proc)

    rc, out = canflash.run_subprocess(["BootCommander", "..."], timeout_s=30)
    assert rc == 0
    assert not proc.killed
    assert "OK" in out


def test_default_timeout_leaves_room_for_a_real_flash():
    """The largest image here is ~54 kB and flashes in well under a minute."""
    assert canflash.DEFAULT_FLASH_TIMEOUT_S >= 120
