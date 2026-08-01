"""Stage 5 - assert the DBC files, firmware #defines, Docs/CAN_Bus.md and the
unit-test layout table all describe the same protocol.

Proving statically how many bytes a C handler consumes would mean parsing C
control flow, which is fragile and would give false confidence. The stage 2
pack/unpack tests carry the byte-layout claim by execution; this stage checks
the other descriptions agree with it.
"""
import json
import re
from pathlib import Path

from vv.boards import BOARDS, REPO_ROOT
from vv.result import StageResult

LAYOUTS_PATH = REPO_ROOT / "vv" / "unit" / "layouts.json"
BUS_DOC = REPO_ROOT / "Docs" / "CAN_Bus.md"

# Per-device sub-blocks from Docs/CAN_Bus.md section 2. The knob is exempt and
# therefore absent.
SUB_BLOCKS = {
    "kincodrive": (0x101, 0x12F),
    "powerstage": (0x130, 0x15F),
    "leddriver": (0x160, 0x17F),
}

# Tolerates the three forms that occur in the real headers:
#   #define CAN_ID_BOOTLOADER   0x101U          <- integer suffix (KincoDrive)
#   #define LED_DEVICEID        0x160  /* ... */ <- trailing comment (LEDDriver)
#   #define DEVICE_ADDR         (0x130)          <- parenthesised
# The original pattern anchored on \s*$ straight after the literal, so it parsed
# ZERO ids from two of the four boards and every DBC message on those boards was
# then reported missing from firmware.
_DEFINE_RE = re.compile(
    r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\(?\s*(0[xX][0-9A-Fa-f]+)[uUlL]*\s*\)?"
    r"\s*(?:/[/*].*)?$",
    re.MULTILINE,
)
_DOC_ROW_RE = re.compile(r"^\|\s*`0x([0-9A-Fa-f]{3})`\s*\|\s*([^|]+?)\s*\|"
                         r"\s*\w+\s*\|\s*([0-9-]+)\s*\|", re.MULTILINE)

# A Fabrica CAN id is an 11-bit standard identifier at or above 0x100.
# Docs/CAN_Bus.md section 2 reserves 0x000-0x0FF for CANopen NMT/SYNC/EMCY, so
# nothing below 0x100 is one of ours. This keeps bitmask constants that happen to
# be hex - EEPROM_CMD_SAVE 0x1, EEPROM_CMD_LOAD_DEFAULT 0x2 - out of the id set.
CAN_ID_MIN = 0x100
CAN_ID_MAX = 0x7FF

# Signals that are padding rather than payload, and so must not determine a
# message's byte order.
_RESERVED_SIGNAL_RE = re.compile(r"reserved|padding|rsvd|spare", re.IGNORECASE)


def parse_defines(path: Path) -> dict[str, int]:
    """Return {MACRO: value} for every #define naming a plausible CAN id."""
    text = path.read_text(encoding="utf-8", errors="replace")
    out = {}
    for m in _DEFINE_RE.finditer(text):
        value = int(m.group(2), 16)
        if CAN_ID_MIN <= value <= CAN_ID_MAX:
            out[m.group(1)] = value
    return out


def dbc_messages(path: Path) -> dict[int, dict]:
    """Return {frame_id: {name, dlc, byte_order}}.

    byte_order is derived from the message's multi-byte signals. cantools reports
    per-signal order as "big_endian" (Motorola) or "little_endian" (Intel). A
    message with no multi-byte signal has byte_order None, and is skipped by the
    byte-order comparison rather than guessed at.
    """
    import cantools
    db = cantools.database.load_file(str(path), strict=False)
    out = {}
    for m in db.messages:
        # Only real multi-byte payload determines byte order. Padding
        # placeholders are excluded: BCAST_EEPROM's sole >8-bit signal is
        # Cfg_Reserved, and letting a reserved field decide the message's
        # endianness produced a mismatch about data that does not exist.
        orders = {
            s.byte_order for s in m.signals
            if s.length > 8 and not _RESERVED_SIGNAL_RE.search(s.name)
        }
        if len(orders) == 1:
            order = "big" if orders.pop() == "big_endian" else "little"
        else:
            order = None  # no real multi-byte signal, or inconsistent within one
        out[m.frame_id] = {"name": m.name, "dlc": m.length, "byte_order": order}
    return out


def doc_messages() -> dict[int, dict]:
    """Parse the message tables in Docs/CAN_Bus.md."""
    text = BUS_DOC.read_text(encoding="utf-8", errors="replace")
    out = {}
    for m in _DOC_ROW_RE.finditer(text):
        dlc = m.group(3).strip()
        out[int(m.group(1), 16)] = {
            "name": m.group(2).strip(),
            "dlc": int(dlc) if dlc.isdigit() else None,
        }
    return out


def load_layouts() -> dict:
    if not LAYOUTS_PATH.is_file():
        return {}
    return json.loads(LAYOUTS_PATH.read_text(encoding="utf-8")).get("boards", {})


# Sub-block boundary markers such as CAN_ID_RANGE_LOW / CAN_ID_RANGE_HIGH name a
# range, not a message, so they are not expected to appear in a DBC.
_RANGE_MARKER_RE = re.compile(r"_RANGE_(LOW|HIGH)$")


def compare_defines_to_dbc(board_id: str, defines: dict[str, int],
                           dbc: dict[int, dict],
                           extra_known_ids: set[int] | None = None) -> list[dict]:
    """Compare a board's firmware ids against its DBC, both directions.

    extra_known_ids covers ids the firmware declares somewhere other than the
    scanned headers - specifically the bootloader RX/TX pair, which lives in the
    bootloader's App/blt_conf.h rather than the application's header. Without it
    every board's Bootloader_TX message looks missing from firmware.
    """
    problems = []
    define_ids = set(defines.values()) | (extra_known_ids or set())
    for name, value in defines.items():
        if _RANGE_MARKER_RE.search(name):
            continue
        if value not in dbc:
            problems.append({"board": board_id, "kind": "define_not_in_dbc",
                             "id": value, "name": name})
    for fid, msg in dbc.items():
        if fid not in define_ids:
            problems.append({"board": board_id, "kind": "dbc_not_in_firmware",
                             "id": fid, "name": msg["name"]})
    return problems


def compare_layouts_to_dbc(board_id: str, layouts: list[dict],
                           dbc: dict[int, dict]) -> list[dict]:
    problems = []
    for entry in layouts:
        msg = dbc.get(entry["id"])
        if msg is None:
            continue  # covered by compare_defines_to_dbc
        if msg["dlc"] != entry["dlc"]:
            problems.append({"board": board_id, "kind": "dlc_mismatch",
                             "id": entry["id"], "name": entry["name"],
                             "dbc_dlc": msg["dlc"], "test_dlc": entry["dlc"]})
        if msg["byte_order"] is not None and msg["byte_order"] != entry["byte_order"]:
            problems.append({"board": board_id, "kind": "byte_order_mismatch",
                             "id": entry["id"], "name": entry["name"],
                             "dbc_order": msg["byte_order"],
                             "test_order": entry["byte_order"]})
    return problems


def compare_doc_to_dbc(board_id: str, dbc: dict[int, dict],
                       doc: dict[int, dict]) -> list[dict]:
    """Report ids documented for this board that no DBC message provides.

    Every other comparison iterates the DBC, so an id that exists ONLY in
    Docs/CAN_Bus.md is invisible to them. That is exactly the case of 0x158
    Bcast_OC_Cfg_B: documented as a 500 ms broadcast, never implemented and not
    in the DBC either. Attribution to a board is by sub-block, so this only runs
    for boards that have one.
    """
    if board_id not in SUB_BLOCKS:
        return []
    lo, hi = SUB_BLOCKS[board_id]
    return [
        {"board": board_id, "kind": "doc_not_in_dbc", "id": fid,
         "name": msg["name"]}
        for fid, msg in doc.items()
        if lo <= fid <= hi and fid not in dbc
    ]


def check_address_plan(board, defines: dict[str, int]) -> list[dict]:
    """Every id must sit inside the board's sub-block from Docs/CAN_Bus.md section 2."""
    if board.address_plan_exempt:
        return []
    lo, hi = SUB_BLOCKS[board.id]
    return [
        {"board": board.id, "kind": "id_outside_sub_block", "id": value,
         "name": name, "block": f"0x{lo:03X}-0x{hi:03X}"}
        for name, value in defines.items()
        if not (lo <= value <= hi)
    ]


def compare_dbc_to_doc(board_id: str, dbc: dict[int, dict],
                       doc: dict[int, dict]) -> list[dict]:
    problems = []
    for fid, msg in dbc.items():
        if fid not in doc:
            problems.append({"board": board_id, "kind": "dbc_not_in_doc",
                             "id": fid, "name": msg["name"]})
        elif doc[fid]["dlc"] is not None and doc[fid]["dlc"] != msg["dlc"]:
            problems.append({"board": board_id, "kind": "doc_dlc_mismatch",
                             "id": fid, "name": msg["name"],
                             "doc_dlc": doc[fid]["dlc"], "dbc_dlc": msg["dlc"]})
    return problems


def check_board(board) -> list[dict]:
    if board.dbc is None:
        return []
    dbc = dbc_messages(REPO_ROOT / board.dbc)
    defines = {}
    for header in board.headers:
        defines.update(parse_defines(REPO_ROOT / header))

    problems = compare_defines_to_dbc(board.id, defines, dbc,
                                      extra_known_ids={board.blt_rx, board.blt_tx})
    problems += compare_layouts_to_dbc(board.id, load_layouts().get(board.id, []), dbc)
    problems += check_address_plan(board, defines)
    if board.in_bus_doc:
        doc = doc_messages()
        problems += compare_dbc_to_doc(board.id, dbc, doc)
        problems += compare_doc_to_dbc(board.id, dbc, doc)

    # Bootloader ids must match blt_conf.h.
    blt_conf = REPO_ROOT / board.boot_dir / "App" / "blt_conf.h"
    if blt_conf.is_file():
        conf = blt_conf.read_text(encoding="utf-8", errors="replace")
        for macro, expected in (("BOOT_COM_CAN_RX_MSG_ID", board.blt_rx),
                                ("BOOT_COM_CAN_TX_MSG_ID", board.blt_tx)):
            m = re.search(rf"#define\s+{macro}\s+\((0[xX][0-9A-Fa-f]+)", conf)
            if m and int(m.group(1), 16) != expected:
                problems.append({"board": board.id, "kind": "blt_id_mismatch",
                                 "macro": macro, "in_file": m.group(1),
                                 "expected": hex(expected)})
    return problems


def run() -> StageResult:
    problems, notes = [], []
    for board in BOARDS:
        if board.dbc is None:
            notes.append({"board": board.id, "kind": "no_dbc",
                          "detail": "board has no DBC and is absent from "
                                    "Docs/CAN_Bus.md; conformance not checked"})
            continue
        problems.extend(check_board(board))

    if not LAYOUTS_PATH.is_file():
        notes.append({"kind": "no_layouts",
                      "detail": "vv/unit/layouts.json missing; run the unit stage first"})

    items = problems + notes
    if problems:
        kinds = sorted({p["kind"] for p in problems})
        return StageResult("conformance", "fail",
                           f"{len(problems)} mismatch(es): {', '.join(kinds)}", items)
    if notes:
        return StageResult("conformance", "warn",
                           f"{len(notes)} unchecked area(s)", items)
    return StageResult("conformance", "pass", "protocol descriptions agree", items)


run.stage_name = "conformance"
