"""Tests for reading, writing, verifying and persisting board configuration.

The board double is not a stub that returns canned answers: it decodes the
command frames it is sent and updates its own telemetry through the same
live-echo map the tool reads back through. A wrong entry in that map therefore
shows up here as a failed round trip, which is the point.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import can  # noqa: E402

from fabrica import config, params  # noqa: E402
from fabrica.canbus import load_dbc  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]
APP = REPO_ROOT / "STM32G0B1_Applciationprog"
POWERSTAGE_DBC = APP / "PowerStage" / "PowerStage.dbc"
KINCODRIVE_DBC = (APP / "KincoDrive_ControlModule_V5_4"
                  / "KincoDrive_ControlModule.dbc")
BUTTONBOARD_DBC = APP / "STM32G0_BUTTONBOARD_PROG" / "ButtonBoard.dbc"


class Board:
    """A board that behaves. Round-robins telemetry and honours commands.

    ``recv`` cycles the readback messages so one ``collect`` sees each once.
    ``send`` decodes the command and pushes each written signal into whatever
    live echo the parameter map declares for it, exactly as real firmware
    would - so a mis-declared echo breaks the round trip rather than being
    papered over.
    """

    def __init__(self, db, board_id: str, deaf: bool = False):
        self.db = db
        self.board_id = board_id
        self.deaf = deaf          # accepts frames but never acts on them
        self.sent: list[can.Message] = []
        self._cursor = 0
        self.state: dict[str, dict] = {}
        for name in sorted(params.readback_messages(board_id)):
            msg = db.get_message_by_name(name)
            self.state[name] = {s.name: 0 for s in msg.signals}

    # -- bus interface ----------------------------------------------------

    def recv(self, timeout=0.0):
        names = sorted(self.state)
        if not names:
            return None
        name = names[self._cursor % len(names)]
        self._cursor += 1
        msg = self.db.get_message_by_name(name)
        return can.Message(arbitration_id=msg.frame_id,
                           data=msg.encode(self.state[name]),
                           timestamp=float(self._cursor),
                           is_extended_id=False)

    def send(self, msg: can.Message):
        self.sent.append(msg)
        if self.deaf:
            return
        try:
            decoded = self.db.decode_message(msg.arbitration_id, msg.data)
            name = self.db.get_message_by_frame_id(msg.arbitration_id).name
        except Exception:
            return
        for grp in params.for_board(self.board_id).groups:
            if grp.message != name:
                continue
            for p in grp.params:
                if p.live is None or p.signal not in decoded:
                    continue
                value = decoded[p.signal]
                # Firmware stores a number, not an enumeration name, and the
                # telemetry signal need not spell its choices the same way.
                value = getattr(value, "value", value)
                target = self.state.setdefault(p.live.message, {})
                if p.live.bits:
                    for index, bit in enumerate(p.live.bits):
                        target[bit] = 1 if int(value) & (1 << index) else 0
                else:
                    target[p.live.signal] = value

    def shutdown(self):
        pass


class BoardImages:
    """Minimal stand-in for manifest.BoardImages: only what config.py reads."""

    def __init__(self, board_id: str):
        self.id = board_id
        self.extended = False


@pytest.fixture
def ps_db():
    if not POWERSTAGE_DBC.is_file():
        pytest.skip(f"{POWERSTAGE_DBC} not found")
    return load_dbc(POWERSTAGE_DBC)


@pytest.fixture
def kd_db():
    if not KINCODRIVE_DBC.is_file():
        pytest.skip(f"{KINCODRIVE_DBC} not found")
    return load_dbc(KINCODRIVE_DBC)


@pytest.fixture
def bb_db():
    if not BUTTONBOARD_DBC.is_file():
        pytest.skip(f"{BUTTONBOARD_DBC} not found")
    return load_dbc(BUTTONBOARD_DBC)


@pytest.fixture
def ps(ps_db):
    return Board(ps_db, "powerstage"), BoardImages("powerstage")


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------


def test_collect_populates_live_and_stored_separately(ps, ps_db):
    bus, _ = ps
    bus.state["BCAST_FAN"]["Fan_Duty_State"] = 70
    bus.state["BCAST_EEPROM"]["Cfg_Fan_Def_Duty"] = 40

    state = config.collect(bus, ps_db, "powerstage", timeout=1.0)

    value = state.get("Fan_Duty")
    assert value.live == 70 and value.live_seen
    assert value.stored == 40 and value.stored_seen
    assert state.missing == ()


def test_collect_reports_what_never_arrived(ps_db):
    """A board that broadcasts nothing must say so, not report zeros."""
    class Silent:
        def recv(self, timeout=0.0):
            return None

    state = config.collect(Silent(), ps_db, "powerstage", timeout=0.05)
    assert set(state.missing) == params.readback_messages("powerstage")
    assert state.get("Fan_Duty").live_seen is False
    assert state.as_dict() == {}


def test_readback_prefers_live_but_falls_back_to_stored(kd_db):
    """KincoDrive OC thresholds have no live echo; stored is all there is."""
    bus = Board(kd_db, "kincodrive")
    bus.state["Bcast_Config_A"]["Cfg_OC_DR_mA"] = 4500
    state = config.collect(bus, kd_db, "kincodrive", timeout=1.0)
    assert state.get("OC_DR_mA").readback() == 4500


def test_buttonboard_mask_is_recombined_from_per_bit_telemetry(bb_db):
    bus = Board(bb_db, "buttonboard")
    for bit in ("Led_1", "Led_3", "Led_6"):
        bus.state["LEDSTATE"][bit] = 1
    state = config.collect(bus, bb_db, "buttonboard", timeout=1.0)
    assert state.get("Led_Mask").live == 0b100101


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------


def test_write_and_verify_round_trips(ps, ps_db):
    bus, board = ps
    results, _ = config.write_and_verify(bus, ps_db, board, "CMD_FAN",
                                         {"Fan_Duty": 65}, timeout=1.0)
    duty = next(r for r in results if r.signal == "Fan_Duty")
    assert duty.live_status == config.MATCH
    assert duty.ok
    assert len(bus.sent) == 1
    assert bus.sent[0].arbitration_id == 0x140


def test_a_write_the_board_ignores_is_reported_as_differ(ps_db):
    bus = Board(ps_db, "powerstage", deaf=True)
    results, _ = config.write_and_verify(bus, ps_db, BoardImages("powerstage"),
                                         "CMD_FAN", {"Fan_Duty": 65},
                                         timeout=1.0)
    duty = next(r for r in results if r.signal == "Fan_Duty")
    assert duty.live_status == config.DIFFER
    assert not duty.ok


def test_unedited_signals_are_seeded_from_the_board_not_zeroed(ps, ps_db):
    """The frame carries the whole message, so siblings must survive a write."""
    bus, board = ps
    bus.state["BCAST_FAN"]["Fan_Auto_On_Temp_State"] = 55

    config.write_and_verify(bus, ps_db, board, "CMD_FAN", {"Fan_Duty": 20},
                            timeout=1.0)

    sent = ps_db.decode_message(bus.sent[0].arbitration_id, bus.sent[0].data)
    assert sent["Fan_Auto_On_Temp"] == 55, "sibling signal was zeroed"
    assert sent["Fan_Duty"] == 20


def test_a_signal_that_cannot_be_seeded_refuses_rather_than_sending_zero(ps, ps_db):
    """CMD_PAGE_DWELL has no readback at all, so a partial write is refused."""
    bus, board = ps
    with pytest.raises(config.ConfigError, match="would encode it as zero"):
        config.write_and_verify(bus, ps_db, board, "CMD_PAGE_DWELL",
                                {"Page_Dwell_Overview": 4}, timeout=0.05)


def test_writing_a_signal_the_message_does_not_carry_is_refused(ps, ps_db):
    bus, board = ps
    with pytest.raises(config.ConfigError, match="has no signal"):
        config.write_and_verify(bus, ps_db, board, "CMD_FAN",
                                {"Fan_Nope": 1}, timeout=0.05)


def test_dry_run_sends_nothing(ps, ps_db):
    bus, board = ps
    config.write_and_verify(bus, ps_db, board, "CMD_FAN", {"Fan_Duty": 65},
                            timeout=1.0, dry_run=True)
    assert bus.sent == []


def test_enum_and_float_readbacks_compare_equal():
    assert config._equal(70.0, 70)
    assert config._equal(70, 70.0)
    assert not config._equal(None, 0)
    assert not config._equal(69, 70)


def test_enumerated_signal_round_trips_as_a_number(ps, ps_db):
    """Fan_Mode is enumerated in the DBC, but canbus decodes choices off.

    So a written 2 is compared against a read-back 2, never against "AUTO".
    Pinned because the decode policy lives in another module: if canbus ever
    turns choices on, the comparison has to keep working and this is where
    that shows up.
    """
    bus, board = ps
    assert ps_db.get_message_by_name("CMD_FAN").get_signal_by_name(
        "Fan_Mode").choices, "expected VAL_ on Fan_Mode"

    results, _ = config.write_and_verify(bus, ps_db, board, "CMD_FAN",
                                         {"Fan_Mode": 2, "Fan_Duty": 10},
                                         timeout=1.0)
    mode = next(r for r in results if r.signal == "Fan_Mode")
    assert mode.live == 2
    assert mode.live_status == config.MATCH
    assert mode.ok


def test_seeding_unwraps_an_enumerated_readback(ps_db):
    """A seeded value must reach cantools as a number, not a choice name.

    The echo signal's choice names are not required to match the command
    signal's, and cantools raises a bare KeyError when they do not.
    """
    from cantools.database.namedsignalvalue import NamedSignalValue

    assert config._numeric(NamedSignalValue(2, "AUTO")) == 2
    assert config._numeric(70) == 70
    assert config._numeric(70.5) == 70.5


def test_a_write_seeded_from_an_enumerated_echo_still_encodes(bb_db):
    """ButtonBoard Led_Source is enumerated on both sides; seeding must work."""
    bus = Board(bb_db, "buttonboard")
    board = BoardImages("buttonboard")
    config.write_and_verify(bus, bb_db, board, "CMD_LED",
                            {"Led_Mask": 0b101}, timeout=1.0)
    sent = bb_db.decode_message(bus.sent[0].arbitration_id, bus.sent[0].data)
    assert sent["Led_Mask"] == 0b101


# ---------------------------------------------------------------------------
# Persisting
# ---------------------------------------------------------------------------


def test_persist_uses_this_boards_save_encoding(ps, ps_db):
    bus, board = ps
    arb_id, data = config.persist(bus, ps_db, board)
    assert arb_id == 0x143
    assert ps_db.decode_message(arb_id, data)["EEPROM_Cmd"] == "SAVE_CONFIG"


def test_load_defaults_differs_between_boards(ps, ps_db, kd_db):
    """0 restores defaults on KincoDrive and is not a command on PowerStage."""
    ps_bus, ps_board = ps
    _, ps_data = config.load_defaults(ps_bus, ps_db, ps_board)
    assert ps_data[0] == 2

    kd_bus = Board(kd_db, "kincodrive")
    _, kd_data = config.load_defaults(kd_bus, kd_db, BoardImages("kincodrive"))
    assert kd_data[0] == 0


def test_persist_on_a_board_without_an_eeprom_command_is_refused(ps_db):
    class Knob:
        id = "knob"
        extended = False

    with pytest.raises(config.ConfigError, match="no EEPROM command"):
        config.persist(None, ps_db, Knob())


def test_persist_dry_run_sends_nothing(ps, ps_db):
    bus, board = ps
    config.persist(bus, ps_db, board, dry_run=True)
    assert bus.sent == []


# ---------------------------------------------------------------------------
# Profiles
# ---------------------------------------------------------------------------


def test_profile_round_trips(tmp_path):
    path = config.profile_path(tmp_path, "powerstage")
    config.save_profile(path, "powerstage", {"Fan_Duty": 40}, note="bench")
    raw = config.load_profile(path)
    assert raw["values"] == {"Fan_Duty": 40}
    assert raw["board"] == "powerstage"
    assert raw["note"] == "bench"


def test_profile_with_the_wrong_schema_is_refused(tmp_path):
    path = tmp_path / "p.json"
    path.write_text(json.dumps({"schema": 99, "values": {}}), encoding="utf-8")
    with pytest.raises(config.ConfigError, match="schema"):
        config.load_profile(path)


def test_profile_that_is_not_json_says_so(tmp_path):
    path = tmp_path / "p.json"
    path.write_text("{oops", encoding="utf-8")
    with pytest.raises(config.ConfigError, match="not valid JSON"):
        config.load_profile(path)


def test_validate_profile_names_unknown_parameters():
    raw = {"values": {"Fan_Duty": 1, "Nope": 2}}
    assert config.validate_profile("powerstage", raw) == ["Nope"]


def test_diff_marks_only_what_actually_differs(ps, ps_db):
    bus, _ = ps
    bus.state["BCAST_FAN"]["Fan_Duty_State"] = 40
    state = config.collect(bus, ps_db, "powerstage", timeout=1.0)
    diffs = config.diff("powerstage",
                        {"values": {"Fan_Duty": 40, "Fan_Min_Duty": 25}}, state)
    changed = {d.signal for d in diffs if d.changed}
    assert changed == {"Fan_Min_Duty"}


def test_apply_writes_and_verifies_a_profile(ps, ps_db):
    bus, board = ps
    report = config.apply_profile(
        bus, ps_db, board,
        {"values": {"Fan_Duty": 55, "Fan_Min_Duty": 15}}, timeout=1.0)
    assert report.ok
    assert report.written == ["CMD_FAN"]
    assert {r.signal for r in report.results} == {"Fan_Duty", "Fan_Min_Duty"}
    assert len(bus.sent) == 1, "both signals share a message: one frame"


def test_apply_skips_parameters_the_board_already_agrees_with(ps, ps_db):
    bus, board = ps
    bus.state["BCAST_FAN"]["Fan_Duty_State"] = 55
    report = config.apply_profile(bus, ps_db, board,
                                  {"values": {"Fan_Duty": 55}}, timeout=1.0)
    assert report.skipped == ["Fan_Duty"]
    assert bus.sent == [], "a matching profile must not put frames on the bus"


def test_apply_refuses_a_profile_naming_an_unknown_parameter(ps, ps_db):
    bus, board = ps
    with pytest.raises(config.ConfigError, match="does not have"):
        config.apply_profile(bus, ps_db, board, {"values": {"Nope": 1}},
                             timeout=0.05)


def test_actuating_parameters_are_skipped_without_allow_actuate(ps, ps_db):
    bus, board = ps
    report = config.apply_profile(bus, ps_db, board,
                                  {"values": {"HS_Cmd_DRIVE": 1}}, timeout=1.0)
    assert report.skipped == ["HS_Cmd_DRIVE"]
    assert bus.sent == []


def test_actuating_parameters_are_written_with_allow_actuate(ps, ps_db):
    bus, board = ps
    report = config.apply_profile(bus, ps_db, board,
                                  {"values": {"HS_Cmd_DRIVE": 1}},
                                  allow_actuate=True, timeout=1.0)
    assert report.written == ["CMD_HS"]
    assert len(bus.sent) == 1


def test_group_overrides_buckets_by_message():
    got = config.group_overrides("powerstage",
                                 {"Fan_Duty": 1, "UV_V24_mV": 2})
    assert got == {"CMD_FAN": {"Fan_Duty": 1}, "CMD_UV": {"UV_V24_mV": 2}}
