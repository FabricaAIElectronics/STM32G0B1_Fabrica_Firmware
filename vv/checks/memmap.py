"""Stage - verify what the .srec files actually write to flash.

The size stage asks the compiler how big things are. This stage asks the
artifact itself where its bytes land, which is the question that matters when a
bootloader and an application are flashed to the same chip by different tools.

It answers three things per board:

  1. does the bootloader stay inside its reserved region
  2. does the application stay at or above its load address
  3. do the two overlap anywhere

Overlap is the failure that hurts. Nothing at flash time would object: ST-Link
writes the bootloader, BootCommander writes the application over the top of part
of it, and the board bricks on the next reset with no diagnostic.

Why the .srec and not `arm-none-eabi-size`: for these bootloaders `size` reports
10180 bytes of text where the .srec writes 8916. The OpenBLT-derived linker
script marks .bss as ALLOC,READONLY and .data as READONLY,CODE, and berkeley
format counts read-only allocated sections as text -- so `size` folds .bss
(1264 B) into flash and reports data=0. The S-records contain exactly the bytes
that reach the device, so they are the authority.

This stage needs no toolchain at all, only the .srec files, so it runs on
machines where the build stage has to skip.
"""
from pathlib import Path

from vv.boards import BOARDS, REPO_ROOT
from vv.result import StageResult

#: S-record type -> address field width in bytes.
_ADDR_BYTES = {"1": 2, "2": 3, "3": 4}


def srec_ranges(path: Path) -> list[tuple[int, int]]:
    """Return merged, sorted [start, end) byte ranges an S-record file writes.

    Only data records (S1/S2/S3) place bytes. S0 is a header, S5/S6 are counts,
    and S7/S8/S9 are start addresses - none of them write flash.
    """
    spans: list[tuple[int, int]] = []
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        line = line.strip()
        if len(line) < 4 or line[0] != "S":
            continue
        addr_bytes = _ADDR_BYTES.get(line[1])
        if addr_bytes is None:
            continue
        count = int(line[2:4], 16)          # address + data + checksum
        addr = int(line[4:4 + addr_bytes * 2], 16)
        data_len = count - addr_bytes - 1
        if data_len > 0:
            spans.append((addr, addr + data_len))

    spans.sort()
    merged: list[tuple[int, int]] = []
    for start, end in spans:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def written_bytes(ranges: list[tuple[int, int]]) -> int:
    return sum(end - start for start, end in ranges)


def overlaps(a: list[tuple[int, int]],
             b: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Every region written by both a and b."""
    out = []
    for s1, e1 in a:
        for s2, e2 in b:
            lo, hi = max(s1, s2), min(e1, e2)
            if lo < hi:
                out.append((lo, hi))
    return out


def _srec_for(project_dir: str, eclipse_name: str) -> Path:
    return REPO_ROOT / project_dir / "Debug" / f"{eclipse_name}.srec"


def check_board(board) -> dict:
    """Analyse one board's pair of artifacts. Never raises."""
    boot_path = _srec_for(board.boot_dir, board.boot_eclipse)
    app_path = _srec_for(board.app_dir, board.app_eclipse)
    result = {"board": board.id, "problems": [], "present": False}

    if not boot_path.is_file() or not app_path.is_file():
        result["detail"] = "artifacts not built"
        return result

    result["present"] = True
    boot = srec_ranges(boot_path)
    app = srec_ranges(app_path)
    if not boot or not app:
        result["problems"].append("an .srec contains no data records")
        return result

    flash_start = 0x08000000
    reserved_end = flash_start + board.boot_reserved_bytes
    flash_end = flash_start + board.flash_total_bytes

    boot_lo, boot_hi = boot[0][0], boot[-1][1]
    app_lo, app_hi = app[0][0], app[-1][1]

    result.update({
        "boot_range": (boot_lo, boot_hi), "app_range": (app_lo, app_hi),
        "boot_bytes": written_bytes(boot), "app_bytes": written_bytes(app),
        "reserved_end": reserved_end,
        "boot_pct": round((boot_hi - flash_start) / board.boot_reserved_bytes * 100, 1),
        "gap": app_lo - boot_hi,
    })

    if boot_lo < flash_start:
        result["problems"].append(
            f"bootloader writes below flash: 0x{boot_lo:08X}")
    if boot_hi > reserved_end:
        result["problems"].append(
            f"bootloader overruns its {board.boot_reserved_bytes // 1024} KB "
            f"reservation by {boot_hi - reserved_end} B "
            f"(ends 0x{boot_hi:08X}, reservation ends 0x{reserved_end:08X})")
    if app_lo < board.app_origin:
        result["problems"].append(
            f"application starts at 0x{app_lo:08X}, below its load address "
            f"0x{board.app_origin:08X}")
    if app_hi > flash_end:
        result["problems"].append(
            f"application runs past end of flash: 0x{app_hi:08X}")

    clash = overlaps(boot, app)
    for lo, hi in clash:
        result["problems"].append(
            f"BOOTLOADER AND APPLICATION OVERLAP at 0x{lo:08X}-0x{hi - 1:08X} "
            f"({hi - lo} B) - flashing one corrupts the other")
    return result


def run() -> StageResult:
    items, problems, present = [], [], 0
    for board in BOARDS:
        r = check_board(board)
        items.append(r)
        if r["present"]:
            present += 1
            problems.extend(f"{r['board']}: {p}" for p in r["problems"])

    if present == 0:
        return StageResult(
            "memmap", "skip", "no .srec artifacts found - build first",
            [{"remedy": "run the build stage, or python vv/run_gate.py"}])
    if problems:
        return StageResult("memmap", "fail",
                           f"{len(problems)} problem(s): {problems[0]}", items)

    worst = max((r["boot_pct"] for r in items if r["present"]), default=0)
    return StageResult(
        "memmap", "pass",
        f"{present} board(s): no overlap, bootloaders within reservation "
        f"(worst {worst}%)", items)


run.stage_name = "memmap"
