"""Tests for the configuration parameter map.

The map is hand-written, so the tests that matter are the ones that hold it
against the real DBCs. A renamed signal must fail here, not on a bench.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from fabrica import params  # noqa: E402
from fabrica.canbus import load_dbc  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]
APP = REPO_ROOT / "STM32G0B1_Applciationprog"

DBCS = {
    "kincodrive": APP / "KincoDrive_ControlModule_V5_4" / "KincoDrive_ControlModule.dbc",
    "powerstage": APP / "PowerStage" / "PowerStage.dbc",
    "leddriver": APP / "STM32G0_LEDDRIVER_PROG" / "LEDDriver.dbc",
    "buttonboard": APP / "STM32G0_BUTTONBOARD_PROG" / "ButtonBoard.dbc",
    "knob": (REPO_ROOT / "STM32F303_Applciationprog" / "Fabrica_STM32F3_Prog"
             / "Knob.dbc"),
}

#: Command messages are the ones a host sends. Bootloader ids are not
#: configuration and are excluded by name.
_CMD_RE = re.compile(r"^(CMD_|Cmd_|KNOBCOMMAND$|DEVICE_ADDR$|DEVICEID$)")
_BOOTLOADER_RE = re.compile(r"BOOTLOADER|Bootloader|DEVICE_ADDR|DEVICEID")


def _db(board_id: str):
    path = DBCS[board_id]
    if not path.is_file():
        pytest.skip(f"{path} not found")
    return load_dbc(path)


@pytest.mark.parametrize("board_id", sorted(DBCS))
def test_map_validates_against_the_real_dbc(board_id):
    """Every message and signal named in the map exists in that board's DBC."""
    assert params.validate(board_id, _db(board_id)) == []


@pytest.mark.parametrize("board_id", sorted(DBCS))
def test_every_command_message_is_modelled_or_declared_an_action(board_id):
    """No command message may be silently absent from the map.

    A new CMD_* added to a DBC should show up as a gap here, not be quietly
    unconfigurable with nothing to say why.
    """
    db = _db(board_id)
    bp = params.for_board(board_id)
    accounted = {g.message for g in bp.groups} | set(bp.actions)
    if bp.persist is not None:
        accounted.add(bp.persist.message)

    commands = {m.name for m in db.messages
                if _CMD_RE.match(m.name) and not _BOOTLOADER_RE.search(m.name)}
    assert commands - accounted == set(), (
        f"{board_id}: command message(s) missing from the parameter map")


def test_echo_requires_exactly_one_of_signal_or_bits():
    with pytest.raises(ValueError, match="exactly one"):
        params.Echo("BCAST_FAN")
    with pytest.raises(ValueError, match="exactly one"):
        params.Echo("BCAST_FAN", signal="Fan_Duty_State", bits=("a",))


def test_validate_reports_a_signal_that_no_longer_exists(monkeypatch):
    """The check has to be able to fail, or passing means nothing."""
    board_id = "powerstage"
    broken = params.BoardParams(groups=(
        params.CommandGroup("CMD_FAN", (
            params.Param("Fan_Duty",
                         live=params.Echo("BCAST_FAN", "Fan_Duty_Renamed")),)),))
    monkeypatch.setitem(params.BOARD_PARAMS, board_id, broken)
    problems = params.validate(board_id, _db(board_id))
    assert len(problems) == 1
    assert "Fan_Duty_Renamed" in problems[0]


def test_kincodrive_oc_threshold_is_stored_only_not_live():
    """Bcast_Config_A is the EEPROM cache, not a live echo of Cmd_OC_Threshold.

    fabrica.verify documents this pairing as the mistake that produced a
    confident false failure against correct firmware. Pin it so the map cannot
    drift back.
    """
    _, param = params.find("kincodrive", "OC_DR_mA")
    assert param.live is None
    assert param.stored == params.Echo("Bcast_Config_A", "Cfg_OC_DR_mA")


def test_kincodrive_fan_duty_has_no_live_echo():
    """Bcast_Fans is tachometer percent - measured, never equal to a command."""
    _, param = params.find("kincodrive", "Fan_DR_Speed")
    assert param.live is None


def test_persist_encodings_differ_per_board():
    """0 is "load defaults" on KincoDrive and not a command at all on PowerStage.

    This is the trap a shared constant would walk into: the same byte wipes one
    board's configuration and does nothing on another's.
    """
    kd = params.for_board("kincodrive").persist
    ps = params.for_board("powerstage").persist
    assert kd.load_defaults == {"EEPROM_Action": 0}
    assert kd.save == {"EEPROM_Action": 1}
    assert ps.save == {"EEPROM_Cmd": 1}
    assert ps.load_defaults == {"EEPROM_Cmd": 2}
    assert kd.load_defaults != ps.load_defaults


def test_leddriver_persist_uses_two_separate_flags():
    ops = params.for_board("leddriver").persist
    assert ops.save == {"EEPROM_Save_Flag": 1, "EEPROM_LoadDefault_Flag": 0}
    assert ops.load_defaults == {"EEPROM_Save_Flag": 0,
                                 "EEPROM_LoadDefault_Flag": 1}


def test_knob_has_no_persist_because_its_protocol_has_none():
    assert params.for_board("knob").persist is None


def test_actuating_parameters_carry_a_description():
    for board_id in DBCS:
        for param in params.actuating(board_id):
            assert param.actuates and param.actuates.strip(), param.signal


def test_rail_enables_are_marked_actuating():
    names = {p.signal for p in params.actuating("powerstage")}
    assert {"HS_Cmd_AUX", "HS_Cmd_DRIVE", "CAN_Relay_Enable"} <= names


def test_thresholds_are_not_marked_actuating():
    names = {p.signal for p in params.actuating("powerstage")}
    assert "OC_Thr_AUX_mA" not in names
    assert "UV_V24_mV" not in names


def test_readback_messages_covers_both_surfaces():
    got = params.readback_messages("powerstage")
    assert "BCAST_FAN" in got          # live
    assert "BCAST_EEPROM" in got       # stored


def test_lookup_helpers():
    grp, param = params.find("powerstage", "Fan_Duty")
    assert grp.message == "CMD_FAN"
    assert param.signal == "Fan_Duty"
    assert "Fan_Duty" in params.signals("powerstage")
    with pytest.raises(KeyError, match="no configurable signal"):
        params.find("powerstage", "Nope")
    with pytest.raises(KeyError, match="no configurable message"):
        params.group("powerstage", "CMD_NOPE")


def test_unmodelled_board_returns_an_empty_set_not_an_error():
    assert params.for_board("no-such-board").groups == ()
    assert params.signals("no-such-board") == []


def test_validate_tolerates_a_board_with_no_dbc():
    assert params.validate("knob", None) == []
