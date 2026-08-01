"""Stage 3 - headless STM32CubeIDE build of all eight projects, Debug only."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from vv.boards import BOARDS, REPO_ROOT
from vv.checks.preflight import find_cubeide
from vv.result import StageResult

_SIZE_RE = re.compile(
    r"^\s*(?P<text>\d+)\s+(?P<data>\d+)\s+(?P<bss>\d+)\s+\d+\s+[0-9a-f]+\s+(?P<elf>\S+\.elf)\s*$",
    re.MULTILINE,
)


def parse_size(log: str) -> dict | None:
    """Pull the last arm-none-eabi-size row out of a build log."""
    matches = list(_SIZE_RE.finditer(log))
    if not matches:
        return None
    m = matches[-1]
    return {
        "text": int(m.group("text")),
        "data": int(m.group("data")),
        "bss": int(m.group("bss")),
        "elf": m.group("elf"),
    }


def build_project(project_dir: str, eclipse_name: str) -> dict:
    """Clean-build one project in a throwaway workspace."""
    cubeide = find_cubeide()
    if cubeide is None:
        return {"project": eclipse_name, "ok": False,
                "errors": ["STM32CubeIDE not found"], "elf": None,
                "text": 0, "data": 0, "bss": 0}

    workspace = Path(tempfile.mkdtemp(prefix=f"vv_ws_{eclipse_name}_"))
    try:
        proc = subprocess.run(
            [cubeide, "--launcher.suppressErrors", "-nosplash",
             "-application", "org.eclipse.cdt.managedbuilder.core.headlessbuild",
             "-data", str(workspace),
             "-import", str(REPO_ROOT / project_dir),
             "-cleanBuild", f"{eclipse_name}/Debug"],
            capture_output=True, text=True, timeout=900,
        )
        log = proc.stdout + proc.stderr
    finally:
        shutil.rmtree(workspace, ignore_errors=True)

    errors = [ln.strip() for ln in log.splitlines() if "error:" in ln]
    size = parse_size(log)
    return {
        "project": eclipse_name,
        "ok": not errors and size is not None,
        "errors": errors[:10],
        "elf": size["elf"] if size else None,
        "text": size["text"] if size else 0,
        "data": size["data"] if size else 0,
        "bss": size["bss"] if size else 0,
    }


def run() -> StageResult:
    items = []
    for board in BOARDS:
        items.append(build_project(board.boot_dir, board.boot_eclipse))
        items.append(build_project(board.app_dir, board.app_eclipse))

    failed = [i["project"] for i in items if not i["ok"]]
    if failed:
        return StageResult("build", "fail", f"failed: {', '.join(failed)}", items)
    return StageResult("build", "pass", f"{len(items)} projects built", items)


run.stage_name = "build"
