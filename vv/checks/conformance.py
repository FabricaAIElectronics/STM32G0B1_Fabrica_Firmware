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

_DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0[xX][0-9A-Fa-f]+)\s*$",
                        re.MULTILINE)
_DOC_ROW_RE = re.compile(r"^\|\s*`0x([0-9A-Fa-f]{3})`\s*\|\s*([^|]+?)\s*\|"
                         r"\s*\w+\s*\|\s*([0-9-]+)\s*\|", re.MULTILINE)


def parse_defines(path: Path) -> dict[str, int]:
    """Return {MACRO: value} for every #define naming a hex constant."""
    text = path.read_text(encoding="utf-8", errors="replace")
    return {m.group(1): int(m.group(2), 16) for m in _DEFINE_RE.finditer(text)}


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
        orders = {s.byte_order for s in m.signals if s.length > 8}
        if len(orders) == 1:
            order = "big" if orders.pop() == "big_endian" else "little"
        else:
            order = None  # none, or inconsistent within the message
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


def compare_defines_to_dbc(board_id: str, defines: dict[str, int],
                           dbc: dict[int, dict]) -> list[dict]:
    problems = []
    define_ids = set(defines.values())
    for name, value in defines.items():
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

    problems = compare_defines_to_dbc(board.id, defines, dbc)
    problems += compare_layouts_to_dbc(board.id, load_layouts().get(board.id, []), dbc)
    problems += check_address_plan(board, defines)
    if board.in_bus_doc:
        problems += compare_dbc_to_doc(board.id, dbc, doc_messages())

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
