"""Tests for the CAN protocol conformance stage."""
from pathlib import Path

from vv.checks import conformance

FIXTURES = Path(__file__).parent / "fixtures"


def test_parse_defines_extracts_hex_ids():
    got = conformance.parse_defines(FIXTURES / "mini_header.h")
    assert got == {"CMD_ALPHA": 0x140, "CMD_BETA": 0x141, "BCAST_GAMMA": 0x150}


def test_dbc_messages_keyed_by_id():
    got = conformance.dbc_messages(FIXTURES / "mini.dbc")
    assert set(got) == {0x140, 0x141}
    assert got[0x140]["name"] == "Cmd_Alpha"
    assert got[0x140]["dlc"] == 2


def test_define_missing_from_dbc_is_reported():
    defines = {"CMD_ALPHA": 0x140, "BCAST_GAMMA": 0x150}
    dbc = {0x140: {"name": "Cmd_Alpha", "dlc": 2, "byte_order": "big"}}
    problems = conformance.compare_defines_to_dbc("powerstage", defines, dbc)
    assert any(p["kind"] == "define_not_in_dbc" and p["id"] == 0x150 for p in problems)


def test_dbc_message_missing_from_firmware_is_reported():
    defines = {"CMD_ALPHA": 0x140}
    dbc = {0x140: {"name": "Cmd_Alpha", "dlc": 2, "byte_order": "big"},
           0x158: {"name": "Bcast_OC_Cfg_B", "dlc": 2, "byte_order": "big"}}
    problems = conformance.compare_defines_to_dbc("powerstage", defines, dbc)
    assert any(p["kind"] == "dbc_not_in_firmware" and p["id"] == 0x158 for p in problems)


def test_dlc_mismatch_between_dbc_and_layouts_is_reported():
    dbc = {0x142: {"name": "Cmd_OC", "dlc": 4, "byte_order": "big"}}
    layouts = [{"id": 0x142, "name": "Cmd_OC", "dlc": 8, "byte_order": "big"}]
    problems = conformance.compare_layouts_to_dbc("powerstage", layouts, dbc)
    assert any(p["kind"] == "dlc_mismatch" and p["id"] == 0x142 for p in problems)


def test_byte_order_mismatch_is_reported():
    dbc = {0x142: {"name": "Cmd_OC", "dlc": 8, "byte_order": "little"}}
    layouts = [{"id": 0x142, "name": "Cmd_OC", "dlc": 8, "byte_order": "big"}]
    problems = conformance.compare_layouts_to_dbc("powerstage", layouts, dbc)
    assert any(p["kind"] == "byte_order_mismatch" and p["id"] == 0x142
               for p in problems)


def test_byte_order_none_is_not_compared():
    dbc = {0x143: {"name": "Cmd_EEPROM", "dlc": 1, "byte_order": None}}
    layouts = [{"id": 0x143, "name": "Cmd_EEPROM", "dlc": 1, "byte_order": "big"}]
    assert conformance.compare_layouts_to_dbc("powerstage", layouts, dbc) == []


def test_id_outside_sub_block_is_reported():
    from vv.boards import board_by_id
    problems = conformance.check_address_plan(
        board_by_id("powerstage"), {"CMD_STRAY": 0x200, "CMD_OK": 0x142})
    assert [p["id"] for p in problems] == [0x200]


def test_exempt_board_skips_the_address_plan_check():
    from vv.boards import board_by_id
    assert conformance.check_address_plan(
        board_by_id("knob"), {"KNOBSTATE": 0x661}) == []


def test_knob_produces_warning_not_failure(monkeypatch):
    monkeypatch.setattr(conformance, "check_board", lambda b: [])
    result = conformance.run()
    assert result.status in ("pass", "warn")
    assert any("knob" in str(i) for i in result.items)
