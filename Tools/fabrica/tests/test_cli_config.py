"""CLI tests for `config` and `flash all`.

No hardware: the bus is the same behaving-board double the config tests use,
and every flash is a dry run or a stubbed runner.
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import sys
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOL_DIR))

import fabrica_cli as cli  # noqa: E402
from fabrica import canbus, config as cfg, params  # noqa: E402

from tests.test_config import Board, POWERSTAGE_DBC  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def fw(tmp_path):
    """A firmware directory whose PowerStage entry points at the real DBC."""
    if not POWERSTAGE_DBC.is_file():
        pytest.skip(f"{POWERSTAGE_DBC} not found")

    root = tmp_path / "firmware"
    boards = []
    for bid, rx, tx, dbc in (("powerstage", "0x130", "0x131", "PowerStage.dbc"),
                             ("leddriver", "0x160", "0x161", None)):
        (root / bid).mkdir(parents=True)
        entry = {"id": bid, "name": bid.title(), "mcu": "STM32G0B1RET6",
                 "can": {"blt_rx": rx, "blt_tx": tx, "bitrate": 500000,
                         "extended": False},
                 "dbc": dbc, "address_plan_exempt": False}
        for kind, addr in (("boot", "0x08000000"), ("app", "0x08003000")):
            name = f"{bid}_{kind}.srec"
            data = f"S0 fake {bid} {kind}\n".encode()
            (root / bid / name).write_bytes(data)
            entry[kind] = {"file": f"{bid}/{name}",
                           "sha256": hashlib.sha256(data).hexdigest(),
                           "flash_bytes": len(data), "load_addr": addr}
        boards.append(entry)

    (root / "powerstage" / "PowerStage.dbc").write_bytes(
        POWERSTAGE_DBC.read_bytes())
    (root / "manifest.json").write_text(json.dumps({
        "schema": 1, "generated": "2026-08-01T00:00:00Z",
        "git_sha": "deadbeefcafe", "git_dirty": False, "gate": "pass",
        "boards": boards}), encoding="utf-8")
    return root


@pytest.fixture
def board_bus(monkeypatch):
    """Every open_bus() in the CLI hands back one behaving PowerStage."""
    bus = Board(canbus.load_dbc(POWERSTAGE_DBC), "powerstage")
    monkeypatch.setattr(canbus, "open_bus", lambda iface, **kw: bus)
    return bus


def run(fw, *argv) -> tuple[int, str]:
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cli.main(["--no-colour", "--firmware", str(fw), *argv])
    return rc, buf.getvalue()


# ------------------------------------------------------------------ read --


def test_read_shows_live_and_stored_columns(fw, board_bus):
    board_bus.state["BCAST_FAN"]["Fan_Duty_State"] = 70
    board_bus.state["BCAST_EEPROM"]["Cfg_Fan_Def_Duty"] = 40
    rc, out = run(fw, "config", "read", "powerstage", "--timeout", "1")
    assert rc == 0
    assert "live=        70" in out
    assert "stored=        40" in out


def test_read_marks_actuating_parameters(fw, board_bus):
    _, out = run(fw, "config", "read", "powerstage", "--timeout", "1")
    assert "[actuates]" in out


def test_read_on_a_board_with_no_dbc_says_why(fw, board_bus):
    with pytest.raises(SystemExit, match="no DBC"):
        run(fw, "config", "read", "leddriver", "--timeout", "1")


# ----------------------------------------------------------------- write --


def test_write_requires_allow_transmit(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "Fan_Duty=70",
                  "--timeout", "1")
    assert rc == 1
    assert "TRANSMITS" in out
    assert board_bus.sent == []


def test_write_lands_and_reports_the_readback(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "Fan_Duty=70",
                  "--allow-transmit", "--timeout", "1")
    assert rc == 0
    assert "OK Fan_Duty" in out
    assert "live=match" in out
    assert len(board_bus.sent) == 1


def test_write_tells_you_it_is_not_yet_persistent(fw, board_bus):
    _, out = run(fw, "config", "write", "powerstage", "Fan_Duty=70",
                 "--allow-transmit", "--timeout", "1")
    assert "Not yet persistent" in out
    assert "config save powerstage" in out


def test_write_refuses_an_actuating_parameter_without_the_flag(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "HS_Cmd_DRIVE=1",
                  "--allow-transmit", "--timeout", "1")
    assert rc == 1
    assert "drive hardware" in out
    assert "switches the DRIVE rail" in out
    assert board_bus.sent == []


def test_write_accepts_an_actuating_parameter_with_the_flag(fw, board_bus):
    rc, _ = run(fw, "config", "write", "powerstage", "HS_Cmd_DRIVE=1",
                "--allow-transmit", "--allow-actuate", "--timeout", "1")
    assert rc == 0
    assert len(board_bus.sent) == 1


def test_write_dry_run_sends_nothing_and_needs_no_transmit_flag(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "Fan_Duty=70",
                  "--dry-run", "--timeout", "1")
    assert rc == 0
    assert "DRY RUN" in out
    assert board_bus.sent == []


def test_write_rejects_a_non_numeric_value(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "Fan_Duty=fast",
                  "--allow-transmit", "--timeout", "1")
    assert rc == 2
    assert "is not a number" in out


def test_write_accepts_hex_for_a_mask(fw, board_bus):
    rc, _ = run(fw, "config", "write", "powerstage", "OC_Thr_AUX_mA=0x1F4",
                "--allow-transmit", "--timeout", "1")
    assert rc == 0
    sent = board_bus.db.decode_message(board_bus.sent[0].arbitration_id,
                                       board_bus.sent[0].data)
    assert sent["OC_Thr_AUX_mA"] == 500


def test_write_of_an_unknown_signal_is_refused(fw, board_bus):
    rc, out = run(fw, "config", "write", "powerstage", "Nope=1",
                  "--allow-transmit", "--timeout", "1")
    assert rc == 1
    assert "no configurable signal" in out


# ------------------------------------------------------- save / defaults --


def test_save_sends_this_boards_save_encoding(fw, board_bus):
    rc, out = run(fw, "config", "save", "powerstage", "--allow-transmit")
    assert rc == 0
    assert "id=0x143" in out
    assert board_bus.sent[-1].data[0] == 1


def test_defaults_sends_the_boards_own_encoding_and_warns(fw, board_bus):
    rc, out = run(fw, "config", "defaults", "powerstage", "--allow-transmit")
    assert rc == 0
    assert board_bus.sent[-1].data[0] == 2
    assert "overwrites the board's stored configuration" in out


def test_save_requires_allow_transmit(fw, board_bus):
    rc, _ = run(fw, "config", "save", "powerstage")
    assert rc == 1
    assert board_bus.sent == []


# --------------------------------------------------------------- profiles --


def test_dump_then_diff_reports_a_match(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    board_bus.state["BCAST_FAN"]["Fan_Duty_State"] = 55

    rc, _ = run(fw, "config", "dump", "powerstage", "--profile", str(profile),
                "--timeout", "1")
    assert rc == 0
    assert json.loads(profile.read_text())["values"]["Fan_Duty"] == 55

    rc, out = run(fw, "config", "diff", "powerstage", "--profile", str(profile),
                  "--timeout", "1")
    assert rc == 0
    assert "matches" in out


def test_diff_reports_and_exits_nonzero_when_it_differs(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    cfg.save_profile(profile, "powerstage", {"Fan_Duty": 99})
    rc, out = run(fw, "config", "diff", "powerstage", "--profile", str(profile),
                  "--timeout", "1")
    assert rc == 1
    assert "Fan_Duty" in out
    assert "1 parameter(s) differ" in out


def test_apply_writes_the_difference(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    cfg.save_profile(profile, "powerstage", {"Fan_Duty": 33})
    rc, out = run(fw, "config", "apply", "powerstage", "--profile",
                  str(profile), "--allow-transmit", "--timeout", "1")
    assert rc == 0
    assert "OK Fan_Duty" in out
    assert board_bus.state["BCAST_FAN"]["Fan_Duty_State"] == 33


def test_apply_with_save_persists(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    cfg.save_profile(profile, "powerstage", {"Fan_Duty": 33})
    rc, out = run(fw, "config", "apply", "powerstage", "--profile",
                  str(profile), "--allow-transmit", "--save", "--timeout", "1")
    assert rc == 0
    assert "saved to EEPROM" in out
    assert board_bus.sent[-1].arbitration_id == 0x143


def test_apply_is_a_noop_when_the_board_already_matches(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    board_bus.state["BCAST_FAN"]["Fan_Duty_State"] = 33
    cfg.save_profile(profile, "powerstage", {"Fan_Duty": 33})
    rc, out = run(fw, "config", "apply", "powerstage", "--profile",
                  str(profile), "--allow-transmit", "--timeout", "1")
    assert rc == 0
    assert "already matches" in out
    assert board_bus.sent == [], "a matching profile must not touch the bus"


def test_apply_refuses_a_profile_with_an_unknown_parameter(fw, board_bus, tmp_path):
    profile = tmp_path / "ps.json"
    cfg.save_profile(profile, "powerstage", {"Nope": 1})
    rc, out = run(fw, "config", "apply", "powerstage", "--profile",
                  str(profile), "--allow-transmit", "--timeout", "1")
    assert rc == 1
    assert "does not have" in out


def test_a_missing_profile_says_so(fw, board_bus, tmp_path):
    rc, out = run(fw, "config", "diff", "powerstage", "--profile",
                  str(tmp_path / "absent.json"), "--timeout", "1")
    assert rc == 1
    assert "no profile at" in out


def test_signal_assignments_are_rejected_for_non_write_actions(fw, board_bus):
    rc, out = run(fw, "config", "read", "powerstage", "Fan_Duty=1")
    assert rc == 2
    assert "takes no SIGNAL=VALUE" in out


# -------------------------------------------------------------- flash all --


def test_flash_all_boot_is_refused_with_the_reason(fw):
    rc, out = run(fw, "flash", "all", "boot")
    assert rc == 2
    assert "ST-Link" in out
    assert "physical probe per board" in out


def test_flash_all_dry_run_covers_every_board(fw, monkeypatch):
    monkeypatch.setattr(cli.env, "find_bootcommander",
                        lambda: "/opt/openblt/Host/BootCommander")
    rc, out = run(fw, "flash", "all", "app", "--dry-run")
    assert rc == 0
    assert "powerstage" in out and "leddriver" in out
    assert "DRY RUN" in out
    assert out.count("BootCommander") >= 2


def test_flash_all_puts_powerstage_last():
    """It can gate the bus it is being flashed over, so it goes last."""
    assert cli.flash_all_order(["powerstage", "leddriver", "kincodrive"])[-1] \
        == "powerstage"
    assert set(cli.flash_all_order(["powerstage", "leddriver"])) == \
        {"powerstage", "leddriver"}


def test_flash_all_stops_at_the_first_failure(fw, board_bus, monkeypatch):
    """A failed flash can leave BootCommander flooding the bus with CONNECT.

    Continuing into that produces a second failure whose real cause is the
    first one, so the default is to stop.
    """
    monkeypatch.setattr(cli.env, "find_bootcommander", lambda: "/bin/false")
    calls = []

    def fake_flash(*a, **kw):
        calls.append(a[4])       # blt_rx
        return cli.canflash.FlashResult(
            ok=False, returncode=1, command=["BootCommander"], output="boom")

    monkeypatch.setattr(cli.canflash, "flash", fake_flash)
    rc, out = run(fw, "flash", "all", "app", "--settle", "0")
    assert rc == 1
    assert len(calls) == 1, "should not have attempted the second board"
    assert "Stopping here" in out
    assert "not attempted" in out


def test_flash_all_keep_going_attempts_every_board(fw, board_bus, monkeypatch):
    monkeypatch.setattr(cli.env, "find_bootcommander", lambda: "/bin/false")
    calls = []

    def fake_flash(*a, **kw):
        calls.append(a[4])
        return cli.canflash.FlashResult(
            ok=False, returncode=1, command=["BootCommander"], output="boom")

    monkeypatch.setattr(cli.canflash, "flash", fake_flash)
    rc, _ = run(fw, "flash", "all", "app", "--keep-going", "--settle", "0")
    assert rc == 1
    assert len(calls) == 2


def test_flash_all_sends_the_bootloader_trigger_first(fw, board_bus, monkeypatch):
    monkeypatch.setattr(cli.env, "find_bootcommander", lambda: "/bin/true")
    monkeypatch.setattr(cli.canflash, "flash", lambda *a, **kw:
                        cli.canflash.FlashResult(ok=True, returncode=0,
                                                 command=["x"], output=""))
    rc, _ = run(fw, "flash", "all", "app", "--settle", "0")
    assert rc == 0
    triggers = [m for m in board_bus.sent if m.data[:1] == b"\xff"]
    assert len(triggers) == 2, "one bootloader trigger per board"
