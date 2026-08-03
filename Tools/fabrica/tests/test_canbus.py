"""Unit tests for fabrica.canbus.

No hardware, no SocketCAN, no wire. Every bus in this file is python-can's
``virtual`` backend, which delivers frames between buses sharing a channel name
entirely in-process. A module-level autouse fixture asserts that no test ever
opens a non-virtual interface, so a stray default cannot quietly try to talk to
a real driver.

The DBC used for decoding is the *real* PowerStage.dbc from the firmware tree,
not a fixture, so a signal layout change in the firmware shows up here.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Tools/fabrica/tests/test_canbus.py -> Tools/fabrica (the package root).
_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import can  # noqa: E402

from fabrica import canbus  # noqa: E402
from fabrica.canbus import (  # noqa: E402
    DbcError,
    DecodedFrame,
    Monitor,
    decode_frame,
    encode_command,
    find_dbc,
    load_dbc,
    message_names,
    open_bus,
    send_frame,
    send_reset,
)

# Tools/fabrica/tests -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[3]
POWERSTAGE_DBC = REPO_ROOT / "STM32G0B1_Applciationprog" / "PowerStage" / "PowerStage.dbc"

# PowerStage bootloader RX id, i.e. the reset trigger target (see PowerStage.dbc
# comment on BO_ 304 and the powerStage preset in flash_can.cfg).
PS_BLT_RX = 0x130

# BCAST_VOLTAGE, 0x152 / 338, 8 bytes:
#   V24_mV VCAP_mV V12_mV all 16-bit big-endian, then a flags byte, then SOC%.
BCAST_VOLTAGE_ID = 338
BCAST_VOLTAGE_DATA = bytes([0x5D, 0xC0, 0x5C, 0xF8, 0x2F, 0x44, 0x00, 87])


@pytest.fixture(autouse=True)
def virtual_only(monkeypatch):
    """Any attempt to open a non-virtual interface is a test bug."""
    real_bus = can.Bus

    def guarded(*args, **kwargs):
        interface = kwargs.get("interface")
        if interface != "virtual":
            raise AssertionError(f"a test tried to open a real bus: interface={interface!r}")
        return real_bus(*args, **kwargs)

    monkeypatch.setattr(canbus.can, "Bus", guarded)


@pytest.fixture
def db():
    if not POWERSTAGE_DBC.is_file():
        pytest.skip(f"PowerStage.dbc not found at {POWERSTAGE_DBC}")
    return load_dbc(POWERSTAGE_DBC)


@pytest.fixture
def bus_pair(request):
    """Two virtual buses sharing a channel: (tx, rx). Closed on teardown."""
    channel = f"fabrica-test-{request.node.name}"
    tx = open_bus(channel, interface="virtual")
    rx = open_bus(channel, interface="virtual")
    yield tx, rx
    tx.shutdown()
    rx.shutdown()


# --------------------------------------------------------------------------
# open_bus / round trip
# --------------------------------------------------------------------------


def test_send_frame_round_trips_between_two_virtual_buses(bus_pair):
    tx, rx = bus_pair
    send_frame(tx, 0x141, b"\x1f")

    msg = rx.recv(timeout=1.0)
    assert msg is not None, "virtual bus delivered nothing"
    assert msg.arbitration_id == 0x141
    assert bytes(msg.data) == b"\x1f"
    assert msg.is_extended_id is False
    assert msg.dlc == 1


def test_send_frame_extended_id_round_trips(bus_pair):
    tx, rx = bus_pair
    send_frame(tx, 0x18FF0102, b"\xde\xad", extended=True)

    msg = rx.recv(timeout=1.0)
    assert msg is not None
    assert msg.arbitration_id == 0x18FF0102
    assert msg.is_extended_id is True


def test_open_bus_requires_a_channel():
    with pytest.raises(ValueError, match="channel name is required"):
        open_bus("", interface="virtual")


def test_send_frame_rejects_out_of_range_standard_id(bus_pair):
    tx, _ = bus_pair
    with pytest.raises(ValueError, match="11-bit standard"):
        send_frame(tx, 0x800, b"\x00")


def test_send_frame_rejects_oversized_payload(bus_pair):
    tx, _ = bus_pair
    with pytest.raises(ValueError, match="at most 8"):
        send_frame(tx, 0x140, b"123456789")


# --------------------------------------------------------------------------
# load_dbc
# --------------------------------------------------------------------------


def test_load_dbc_raises_clear_error_for_missing_file(tmp_path):
    missing = tmp_path / "NoSuchBoard.dbc"
    with pytest.raises(DbcError) as exc:
        load_dbc(missing)
    text = str(exc.value)
    assert "NoSuchBoard.dbc" in text
    assert str(missing) in text


def test_load_dbc_raises_clear_error_for_unparseable_file(tmp_path):
    junk = tmp_path / "Broken.dbc"
    junk.write_text("this is not a dbc file at all\n", encoding="utf-8")
    with pytest.raises(DbcError) as exc:
        load_dbc(junk)
    assert "Broken.dbc" in str(exc.value)


def test_load_dbc_loads_the_real_powerstage_dbc(db):
    names = message_names(db)
    assert "BCAST_VOLTAGE" in names
    assert "CMD_FAN" in names
    assert db.get_message_by_name("BCAST_VOLTAGE").frame_id == BCAST_VOLTAGE_ID


def test_find_dbc_returns_none_for_a_board_without_one(tmp_path):
    # manifest.BoardImages.dbc is None for the knob board.
    assert find_dbc(None, tmp_path) is None


def test_find_dbc_locates_a_file_by_name_under_the_repo():
    if not POWERSTAGE_DBC.is_file():
        pytest.skip("PowerStage.dbc not present")
    found = find_dbc("PowerStage.dbc", REPO_ROOT / "STM32G0B1_Applciationprog")
    assert found is not None
    assert found.name == "PowerStage.dbc"


def test_find_dbc_returns_none_when_the_named_file_is_absent(tmp_path):
    assert find_dbc("Nope.dbc", tmp_path) is None


# --------------------------------------------------------------------------
# decode_frame against the real DBC
# --------------------------------------------------------------------------


def test_decode_real_bcast_voltage_message(db):
    frame = decode_frame(db, BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, timestamp=12.5)

    assert isinstance(frame, DecodedFrame)
    assert frame.name == "BCAST_VOLTAGE"
    assert frame.arb_id == BCAST_VOLTAGE_ID
    assert frame.raw == BCAST_VOLTAGE_DATA
    assert frame.timestamp == 12.5
    assert frame.known and frame.decoded
    assert frame.signals == {
        "V24_mV": 24000,
        "VCAP_mV": 23800,
        "V12_mV": 12100,
        "UV_Fault_V24": 0,
        "UV_Fault_VCAP": 0,
        "UV_Fault_V12": 0,
        "Battery_SOC_Low_Flag": 0,
        "Battery_SOC_pct": 87,
    }


def test_decode_applies_dbc_scaling(db):
    # BCAST_FAN.Temperature_C is signed 16-bit big-endian with factor 0.1.
    # 0x00FA = 250 -> 25.0 degC.
    frame = decode_frame(db, 339, bytes([0x02, 0x4B, 0x00, 0xFA, 0x0A, 0x23, 0x1E]))
    assert frame.name == "BCAST_FAN"
    assert frame.signals["Fan_Duty_State"] == 75
    assert frame.signals["Temperature_C"] == pytest.approx(25.0)


def test_decoded_values_are_plain_numbers_not_named_choices(db):
    # BCAST_FAN.Fan_Mode_State has a VAL_ table; we want 2, not 'AUTO', so the
    # snapshot stays JSON-serialisable.
    frame = decode_frame(db, 339, bytes([0x02, 0x4B, 0x00, 0xFA, 0x0A, 0x23, 0x1E]))
    assert type(frame.signals["Fan_Mode_State"]) is int


# --------------------------------------------------------------------------
# decode_frame never raises
# --------------------------------------------------------------------------


def test_decode_unknown_id_does_not_raise(db):
    frame = decode_frame(db, 0x7AB, b"\x01\x02\x03", timestamp=1.0)
    assert frame.name is None
    assert frame.signals == {}
    assert frame.raw == b"\x01\x02\x03"
    assert frame.arb_id == 0x7AB
    assert frame.known is False


def test_decode_empty_payload_does_not_raise(db):
    frame = decode_frame(db, BCAST_VOLTAGE_ID, b"")
    assert frame.name == "BCAST_VOLTAGE"   # id is known...
    assert frame.signals == {}             # ...but nothing could be decoded
    assert frame.raw == b""


def test_decode_short_payload_does_not_raise(db):
    # BCAST_VOLTAGE is 8 bytes; a board mid-firmware-change sends 3.
    frame = decode_frame(db, BCAST_VOLTAGE_ID, b"\x5d\xc0\x5c")
    assert frame.name == "BCAST_VOLTAGE"
    assert frame.signals == {}
    assert frame.raw == b"\x5d\xc0\x5c"


def test_decode_overlong_payload_does_not_raise(db):
    frame = decode_frame(db, BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA + b"\xff\xff")
    assert frame.name == "BCAST_VOLTAGE"
    assert frame.raw.endswith(b"\xff\xff")


def test_decode_with_no_dbc_does_not_raise():
    # The knob board: manifest dbc is None. Ids and payloads still work.
    frame = decode_frame(None, 0x140, b"\x02\x4b", timestamp=3.5)
    assert frame.name is None
    assert frame.signals == {}
    assert frame.raw == b"\x02\x4b"
    assert frame.arb_id == 0x140
    assert frame.timestamp == 3.5


def test_decode_with_a_broken_database_object_does_not_raise():
    class Exploding:
        def get_message_by_frame_id(self, _):
            raise RuntimeError("corrupt database")

    frame = decode_frame(Exploding(), 0x140, b"\x01")
    assert frame.name is None
    assert frame.raw == b"\x01"


def test_decode_handles_none_data(db):
    frame = decode_frame(db, BCAST_VOLTAGE_ID, None)
    assert frame.raw == b""
    assert frame.signals == {}


# --------------------------------------------------------------------------
# Monitor
# --------------------------------------------------------------------------


def test_monitor_snapshot_is_latest_per_id_sorted_by_id(db):
    mon = Monitor(db)
    mon.observe(339, bytes([0x02, 0x4B, 0x00, 0xFA, 0x0A, 0x23, 0x1E]), 0.0)
    mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, 0.1)
    mon.observe(304, b"\xff\x00", 0.2)
    # Second BCAST_VOLTAGE: SOC drops from 87 to 42. Latest must win.
    later = bytearray(BCAST_VOLTAGE_DATA)
    later[7] = 42
    mon.observe(BCAST_VOLTAGE_ID, bytes(later), 0.3)

    snap = mon.snapshot()
    assert [f.arb_id for f in snap] == [304, 338, 339]      # sorted, one per id
    voltage = next(f for f in snap if f.arb_id == BCAST_VOLTAGE_ID)
    assert voltage.signals["Battery_SOC_pct"] == 42
    assert voltage.timestamp == 0.3


def test_monitor_counts_increment_per_id(db):
    mon = Monitor(db)
    for i in range(5):
        mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, i * 0.1)
    mon.observe(339, bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]), 1.0)

    assert mon.counts() == {338: 5, 339: 1}
    assert mon.ids == [338, 339]
    assert len(mon) == 2


def test_monitor_rate_hz_for_evenly_spaced_timestamps(db):
    mon = Monitor(db)
    for i in range(11):           # 10 intervals of 100 ms -> 10 Hz
        mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, i * 0.1)

    assert mon.rate_hz(BCAST_VOLTAGE_ID) == pytest.approx(10.0, rel=1e-6)


def test_monitor_rate_hz_is_none_for_a_single_observation(db):
    mon = Monitor(db)
    mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, 1.0)
    assert mon.rate_hz(BCAST_VOLTAGE_ID) is None


def test_monitor_rate_hz_is_none_for_an_unseen_id(db):
    mon = Monitor(db)
    assert mon.rate_hz(0x7FF) is None


def test_monitor_rate_hz_is_none_when_timestamps_do_not_advance(db):
    mon = Monitor(db)
    for _ in range(4):
        mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, 5.0)
    assert mon.rate_hz(BCAST_VOLTAGE_ID) is None


def test_monitor_rate_window_is_bounded(db):
    mon = Monitor(db)
    for i in range(200):
        mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, i * 0.01)
    # Counts keep growing, the timestamp window does not.
    assert mon.counts()[BCAST_VOLTAGE_ID] == 200
    assert mon.rate_hz(BCAST_VOLTAGE_ID) == pytest.approx(100.0, rel=1e-6)


def test_monitor_clear_forgets_traffic_but_keeps_the_dbc(db):
    mon = Monitor(db)
    mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, 0.0)
    mon.clear()

    assert mon.snapshot() == []
    assert mon.counts() == {}
    assert mon.rate_hz(BCAST_VOLTAGE_ID) is None
    frame = mon.observe(BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA, 1.0)
    assert frame.name == "BCAST_VOLTAGE"   # db survived the clear


def test_monitor_works_without_a_dbc():
    mon = Monitor()                        # knob board: no DBC at all
    mon.observe(0x140, b"\x02\x4b", 0.0)
    mon.observe(0x140, b"\x02\x00", 0.5)

    snap = mon.snapshot()
    assert len(snap) == 1
    assert snap[0].name is None
    assert snap[0].raw == b"\x02\x00"
    assert mon.counts() == {0x140: 2}
    assert mon.rate_hz(0x140) == pytest.approx(2.0)


def test_monitor_consumes_frames_off_a_virtual_bus(bus_pair, db):
    """End to end: send on one virtual bus, monitor what arrives on the other."""
    tx, rx = bus_pair
    mon = Monitor(db)

    send_frame(tx, BCAST_VOLTAGE_ID, BCAST_VOLTAGE_DATA)
    send_frame(tx, 339, bytes([0x02, 0x4B, 0x00, 0xFA, 0x0A, 0x23, 0x1E]))

    for _ in range(2):
        msg = rx.recv(timeout=1.0)
        assert msg is not None
        mon.observe(msg.arbitration_id, bytes(msg.data), msg.timestamp)

    snap = mon.snapshot()
    assert [f.arb_id for f in snap] == [338, 339]
    assert snap[0].signals["V24_mV"] == 24000
    assert snap[1].signals["Temperature_C"] == pytest.approx(25.0)


# --------------------------------------------------------------------------
# send_reset
# --------------------------------------------------------------------------


def test_send_reset_puts_the_bootloader_trigger_on_the_bus(bus_pair):
    tx, rx = bus_pair
    send_reset(tx, PS_BLT_RX)

    msg = rx.recv(timeout=1.0)
    assert msg is not None
    assert msg.arbitration_id == PS_BLT_RX
    assert bytes(msg.data) == b"\xff\x00"
    assert msg.dlc == 2                    # DLC 2 is part of the trigger
    assert msg.is_extended_id is False


def test_send_reset_decodes_as_device_command_in_the_real_dbc(bus_pair, db):
    """The reset frame is DEVICE_ADDR/Device_Command=0xFF in PowerStage.dbc."""
    tx, rx = bus_pair
    send_reset(tx, PS_BLT_RX)

    msg = rx.recv(timeout=1.0)
    frame = decode_frame(db, msg.arbitration_id, bytes(msg.data), msg.timestamp)
    assert frame.name == "DEVICE_ADDR"
    assert frame.signals["Device_Command"] == 0xFF


def test_send_reset_rejects_a_bad_id(bus_pair):
    tx, _ = bus_pair
    with pytest.raises(ValueError):
        send_reset(tx, 0x20000000)         # beyond 29 bits


# --------------------------------------------------------------------------
# encode_command
# --------------------------------------------------------------------------


def test_encode_command_round_trips_through_decode_frame(db):
    # CMD_FAN is DLC=5 since the AUTO thresholds became settable over CAN.
    # cantools encodes every signal a message declares, so all five are given
    # here. The firmware still accepts the legacy DLC=2 form, but a host
    # driving it from the DBC will always emit the long one.
    fields = {"Fan_Mode": 2, "Fan_Duty": 75, "Fan_Min_Duty": 10,
              "Fan_Auto_On_Temp": 35, "Fan_Auto_Off_Temp": 30}
    arb_id, data = encode_command(db, "CMD_FAN", fields)
    assert arb_id == 320                   # 0x140
    assert data == b"\x02\x4b\x0a\x23\x1e"

    frame = decode_frame(db, arb_id, data)
    assert frame.name == "CMD_FAN"
    assert frame.signals == fields


def test_encode_command_round_trips_a_multibyte_big_endian_message(db):
    signals = {
        "OC_Thr_AUX_mA": 5000,
        "OC_Thr_LED_mA": 3000,
        "OC_Thr_DRIVE_mA": 12000,
        "OC_Thr_CAP_mA": 0,
    }
    arb_id, data = encode_command(db, "CMD_OC", signals)
    assert arb_id == 322                   # 0x142
    assert len(data) == 8
    assert data[:2] == (5000).to_bytes(2, "big")

    assert decode_frame(db, arb_id, data).signals == signals


def test_encode_command_output_survives_a_virtual_bus_round_trip(bus_pair, db):
    tx, rx = bus_pair
    arb_id, data = encode_command(db, "CMD_UV", {
        "UV_V24_mV": 21000, "UV_VCAP_mV": 20000, "UV_V12_mV": 10500,
    })
    send_frame(tx, arb_id, data)

    msg = rx.recv(timeout=1.0)
    frame = decode_frame(db, msg.arbitration_id, bytes(msg.data), msg.timestamp)
    assert frame.name == "CMD_UV"
    assert frame.signals["UV_V24_mV"] == 21000
    assert frame.signals["UV_V12_mV"] == 10500


def test_encode_command_unknown_message_lists_available_names(db):
    with pytest.raises(KeyError) as exc:
        encode_command(db, "CMD_TURBO", {"x": 1})
    text = exc.value.args[0]
    assert "CMD_TURBO" in text
    assert "CMD_FAN" in text               # the menu, not just the failure
    assert "BCAST_VOLTAGE" in text


def test_encode_command_without_a_dbc_is_a_clear_error():
    with pytest.raises(ValueError, match="no DBC"):
        encode_command(None, "CMD_FAN", {"Fan_Mode": 2})


def test_encode_command_rejects_an_out_of_range_signal(db):
    with pytest.raises(Exception):
        encode_command(db, "CMD_FAN", {"Fan_Mode": 2, "Fan_Duty": 70000})


def test_message_names_is_empty_without_a_dbc():
    assert message_names(None) == []


def test_find_dbc_works_the_way_the_cli_actually_calls_it():
    """Regression: search_root used to be required.

    Both real callers -- cmd_monitor and the TUI -- call find_dbc(board.dbc)
    with one argument, so the first board that had a DBC raised TypeError. Every
    unit test passed because every unit test supplied the second argument. This
    test calls it exactly as the application does.
    """
    assert find_dbc("PowerStage.dbc") is not None
    assert find_dbc(None) is None
    assert find_dbc("DefinitelyNotThere.dbc") is None


def test_every_manifest_dbc_name_resolves():
    """The names the manifest uses must actually be findable from a default call."""
    for name in ("PowerStage.dbc", "KincoDrive_ControlModule.dbc", "LEDDriver.dbc"):
        assert find_dbc(name) is not None, f"{name} not resolvable"
