"""Stage 4 - assert each artifact fits the flash region it must occupy.

The F303 bootloader's linker script declares LENGTH = 512K rather than its true
14 KB reservation, so the linker will not catch an overflow there. This stage is
currently the only thing that would.
"""
from vv.boards import BOARDS
from vv.checks.build import build_project
from vv.result import StageResult

WARN_FRACTION = 0.80


def check_artifact(name: str, flash_bytes: int, limit: int) -> dict:
    pct = (flash_bytes / limit) * 100 if limit else 0.0
    if flash_bytes > limit:
        status = "fail"
    elif pct >= WARN_FRACTION * 100:
        status = "warn"
    else:
        status = "pass"
    return {"artifact": name, "flash": flash_bytes, "limit": limit,
            "pct": round(pct, 1), "status": status}


def gather_sizes() -> list[dict]:
    """Build everything and pair each artifact with its region limit."""
    out = []
    for board in BOARDS:
        boot = build_project(board.boot_dir, board.boot_eclipse)
        app = build_project(board.app_dir, board.app_eclipse)
        out.append({
            "artifact": f"{board.id}/boot",
            "flash": boot["text"] + boot["data"],
            "limit": board.boot_reserved_bytes,
        })
        out.append({
            "artifact": f"{board.id}/app",
            "flash": app["text"] + app["data"],
            "limit": board.flash_total_bytes - board.boot_reserved_bytes,
        })
    return out


def run() -> StageResult:
    items = [check_artifact(a["artifact"], a["flash"], a["limit"])
             for a in gather_sizes()]

    failed = [i for i in items if i["status"] == "fail"]
    warned = [i for i in items if i["status"] == "warn"]

    if failed:
        worst = ", ".join(f"{i['artifact']} {i['pct']}%" for i in failed)
        return StageResult("size", "fail", f"over limit: {worst}", items)
    if warned:
        worst = ", ".join(f"{i['artifact']} {i['pct']}%" for i in warned)
        return StageResult("size", "warn", f"above {int(WARN_FRACTION*100)}%: {worst}", items)
    return StageResult("size", "pass", f"{len(items)} artifacts fit", items)


run.stage_name = "size"
