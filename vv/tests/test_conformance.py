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


def test_a_board_without_a_dbc_is_noted_not_failed(monkeypatch):
    """A board with no DBC is unchecked, which is a warning, never a failure.

    The knob used to be that board. It now has one, so this exercises the path
    with a stand-in rather than deleting the coverage - a future board will
    arrive before its DBC does.
    """
    from vv.boards import BOARDS, Board

    dbcless = Board(**{**BOARDS[0].__dict__, "id": "nodbc", "dbc": None})
    monkeypatch.setattr(conformance, "BOARDS", [dbcless])
    monkeypatch.setattr(conformance, "check_board", lambda b: [])
    result = conformance.run()
    assert result.status == "warn"
    assert any(i.get("kind") == "no_dbc" for i in result.items)


def test_every_declared_board_dbc_actually_parses():
    """Guards the knob DBC authored from the firmware."""
    from vv.boards import BOARDS, REPO_ROOT

    for b in BOARDS:
        if b.dbc is None:
            continue
        msgs = conformance.dbc_messages(REPO_ROOT / b.dbc)
        assert msgs, f"{b.id}: {b.dbc} parsed to no messages"


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


# ── The combined all-boards DBC and the header comment blocks ───────────────
# Both are protocol descriptions an operator reads, and neither was checked
# until Docs/Fabrica_Bus.dbc was found carrying PowerStage's pre-AUTO fan DLCs.

def test_combined_dbc_agrees_with_every_per_board_dbc():
    assert conformance.check_combined_dbc() == []


def test_combined_dbc_dlc_drift_is_reported(tmp_path, monkeypatch):
    board = next(b for b in conformance.BOARDS if b.id == "powerstage")
    real = conformance.dbc_messages(conformance.REPO_ROOT / board.dbc)
    fid = next(iter(real))
    stale = {i: dict(m) for i, m in real.items()}
    stale[fid]["dlc"] = real[fid]["dlc"] + 1

    combined = tmp_path / "Fabrica_Bus.dbc"
    combined.write_text("", encoding="utf-8")
    monkeypatch.setattr(conformance, "COMBINED_DBC", combined)
    monkeypatch.setattr(conformance, "dbc_messages",
                        lambda path: stale if Path(path) == combined else real)
    monkeypatch.setattr(conformance, "BOARDS", [board])

    problems = conformance.check_combined_dbc()
    assert [p["kind"] for p in problems] == ["combined_dbc_dlc"]
    assert problems[0]["id"] == f"0x{fid:03X}"


def test_message_absent_from_the_combined_dbc_is_reported(tmp_path, monkeypatch):
    board = next(b for b in conformance.BOARDS if b.id == "powerstage")
    real = conformance.dbc_messages(conformance.REPO_ROOT / board.dbc)

    combined = tmp_path / "Fabrica_Bus.dbc"
    combined.write_text("", encoding="utf-8")
    monkeypatch.setattr(conformance, "COMBINED_DBC", combined)
    monkeypatch.setattr(conformance, "dbc_messages",
                        lambda path: {} if Path(path) == combined else real)
    monkeypatch.setattr(conformance, "BOARDS", [board])

    problems = conformance.check_combined_dbc()
    assert {p["kind"] for p in problems} == {"missing_from_combined_dbc"}
    assert len(problems) == len(real)


def test_header_comment_dlcs_match_the_dbc():
    for board in conformance.BOARDS:
        if board.dbc is None:
            continue
        dbc = conformance.dbc_messages(conformance.REPO_ROOT / board.dbc)
        assert conformance.check_header_comments(board, dbc) == [], board.id


def test_stale_header_comment_dlc_is_reported(tmp_path, monkeypatch):
    header = tmp_path / "can_operation.h"
    header.write_text(" *  CMD_FAN     [0x140] DLC=2\n", encoding="utf-8")
    board = next(b for b in conformance.BOARDS if b.id == "powerstage")
    monkeypatch.setattr(conformance, "REPO_ROOT", tmp_path)
    object.__setattr__(board, "headers", ("can_operation.h",))
    try:
        problems = conformance.check_header_comments(
            board, {0x140: {"name": "Cmd_Fan", "dlc": 5, "byte_order": None}})
    finally:
        object.__setattr__(board, "headers",
                           ("STM32G0B1_Applciationprog/PowerStage/Core/Inc/can_operation.h",))
    assert [p["kind"] for p in problems] == ["header_comment_dlc"]
    assert problems[0]["comment_dlc"] == 2 and problems[0]["dbc_dlc"] == 5
