"""Tests for the CAN protocol conformance stage."""
from pathlib import Path

from vv.checks import conformance

FIXTURES = Path(__file__).parent / "fixtures"


def test_parse_defines_extracts_hex_ids():
    got = conformance.parse_defines(FIXTURES / "mini_header.h")
    assert got == {
        "CMD_ALPHA": 0x140,
        "CMD_BETA": 0x141,
        "BCAST_GAMMA": 0x150,
        "CMD_SUFFIXED": 0x142,
        "CMD_COMMENTED": 0x143,
        "CMD_PARENS": 0x144,
        "CMD_RANGE_HIGH": 0x15F,
    }
    # a quoted string and a sub-0x100 bitmask are not CAN ids
    assert "NOT_AN_ID" not in got
    assert "NOT_AN_ID_MASK" not in got


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


def test_parse_defines_handles_suffix_comment_and_parens():
    """The real headers use all three forms; the original regex parsed none of them."""
    got = conformance.parse_defines(FIXTURES / "mini_header.h")
    assert got["CMD_SUFFIXED"] == 0x142      # 0x142U
    assert got["CMD_COMMENTED"] == 0x143     # trailing /* comment */
    assert got["CMD_PARENS"] == 0x144        # (0x144)


def test_parse_defines_rejects_values_below_the_can_id_floor():
    """Bitmask constants like EEPROM_CMD_SAVE 0x1 are not CAN ids."""
    got = conformance.parse_defines(FIXTURES / "mini_header.h")
    assert "NOT_AN_ID_MASK" not in got


def test_range_markers_are_not_expected_in_the_dbc():
    defines = {"CMD_RANGE_HIGH": 0x15F, "CMD_REAL": 0x140}
    dbc = {0x140: {"name": "Cmd_Real", "dlc": 1, "byte_order": None}}
    problems = conformance.compare_defines_to_dbc("powerstage", defines, dbc)
    assert [p["name"] for p in problems if p["kind"] == "define_not_in_dbc"] == []


def test_bootloader_ids_count_as_known_firmware_ids():
    """blt_rx/blt_tx live in blt_conf.h, not the application header."""
    dbc = {0x131: {"name": "PS_Bootloader_TX", "dlc": 8, "byte_order": None}}
    assert conformance.compare_defines_to_dbc(
        "powerstage", {}, dbc, extra_known_ids={0x130, 0x131}) == []
    assert conformance.compare_defines_to_dbc("powerstage", {}, dbc) != []


def test_doc_only_id_is_reported():
    """0x158 exists only in Docs/CAN_Bus.md; every other check iterates the DBC."""
    dbc = {0x157: {"name": "Bcast_OC_Cfg_A", "dlc": 8, "byte_order": "big"}}
    doc = {0x157: {"name": "BCAST_OC_CFG_A", "dlc": 8},
           0x158: {"name": "BCAST_OC_CFG_B", "dlc": 2}}
    problems = conformance.compare_doc_to_dbc("powerstage", dbc, doc)
    assert [p["id"] for p in problems] == [0x158]


def test_doc_only_check_skipped_for_boards_without_a_sub_block():
    assert conformance.compare_doc_to_dbc("knob", {}, {0x661: {"name": "X", "dlc": 8}}) == []


def test_reserved_signal_does_not_set_message_byte_order(tmp_path):
    """A padding placeholder must not decide a message's endianness.

    BCAST_EEPROM's only >8-bit signal is Cfg_Reserved. Letting it set the
    message byte order produced a mismatch about data that does not exist.
    """
    dbc = tmp_path / "reserved.dbc"
    dbc.write_text(
        'VERSION ""\n\nBS_:\n\nBU_: Tester Device\n\n'
        'BO_ 340 BCAST_THING: 8 Device\n'
        ' SG_ Real_Byte : 0|8@1+ (1,0) [0|255] "" Tester\n'
        ' SG_ Cfg_Reserved : 8|16@1+ (1,0) [0|65535] "" Tester\n',
        encoding="utf-8")
    got = conformance.dbc_messages(dbc)
    assert got[340]["byte_order"] is None, "reserved padding must not set byte order"


def test_real_multibyte_signal_still_sets_byte_order(tmp_path):
    dbc = tmp_path / "real.dbc"
    dbc.write_text(
        'VERSION ""\n\nBS_:\n\nBU_: Tester Device\n\n'
        'BO_ 341 BCAST_OTHER: 8 Device\n'
        ' SG_ Threshold_mA : 0|16@0+ (1,0) [0|65535] "mA" Tester\n',
        encoding="utf-8")
    assert conformance.dbc_messages(dbc)[341]["byte_order"] == "big"
