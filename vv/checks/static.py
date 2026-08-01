"""Stage 1 - compile every project's own sources with aggressive warnings.

Only warnings absent from vv/baseline.txt fail the gate. The tree carries a
large number of pre-existing warnings in the OpenBLT-derived App/ files; a gate
that is red on day one gets ignored, so those are recorded and excluded.
"""
import re
import subprocess
from pathlib import Path

from vv.boards import BOARDS, REPO_ROOT
from vv.result import StageResult

BASELINE_PATH = REPO_ROOT / "vv" / "baseline.txt"

WARN_FLAGS = [
    "-Wall", "-Wextra", "-Wshadow", "-Wundef", "-Wpointer-arith",
    "-Wstrict-prototypes", "-Wlogical-op", "-Wduplicated-cond",
    "-Wduplicated-branches", "-Wnull-dereference", "-Wjump-misses-init",
    "-Wswitch-default", "-Wsign-compare",
]

# Generated or vendored files we do not own.
SKIP_NAMES = {"syscalls.c", "sysmem.c", "flash_layout.c"}
SKIP_PREFIXES = ("system_stm32",)

_WARN_RE = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+):\s*(?P<msg>warning:.*)$")


def _mcu_flags(board):
    if board.mcu.startswith("STM32G0"):
        return ["-mcpu=cortex-m0plus", "-DSTM32G0B1xx"]
    return ["-mcpu=cortex-m4", "-DSTM32F303xE"]



def sources_for(board) -> list[Path]:
    """Every .c file in a board's two projects that we are responsible for."""
    return [path for _, path in sources_with_project(board)]


def sources_with_project(board) -> list[tuple[str, Path]]:
    """(project_dir, source_path) pairs, so callers know which -I set to use."""
    out: list[tuple[str, Path]] = []
    for proj in (board.app_dir, board.boot_dir):
        for sub in ("Core/Src", "App"):
            d = REPO_ROOT / proj / sub
            if not d.is_dir():
                continue
            for f in sorted(d.glob("*.c")):
                if f.name in SKIP_NAMES or f.name.startswith(SKIP_PREFIXES):
                    continue
                out.append((proj, f))
    return out


def _includes(board, project_dir: str) -> list[str]:
    fam = "STM32G0xx" if board.mcu.startswith("STM32G0") else "STM32F3xx"
    p = REPO_ROOT / project_dir
    inc = [
        f"-I{p / 'Core/Inc'}",
        f"-I{p / 'Drivers' / (fam + '_HAL_Driver') / 'Inc'}",
        f"-I{p / 'Drivers' / (fam + '_HAL_Driver') / 'Inc/Legacy'}",
        f"-I{p / 'Drivers/CMSIS/Device/ST' / fam / 'Include'}",
        f"-I{p / 'Drivers/CMSIS/Include'}",
    ]
    if (p / "App").is_dir():
        blt = REPO_ROOT / "ThirdParty/openblt/Target/Source"
        port = "ARMCM0_STM32G0" if board.mcu.startswith("STM32G0") else "ARMCM4_STM32F3"
        inc += [f"-I{p / 'App'}", f"-I{blt}", f"-I{blt / port}", f"-I{blt / port / 'GCC'}"]
    return inc


def collect_warnings() -> list[str]:
    """Compile everything and return normalised warning strings."""
    found: list[str] = []
    for board in BOARDS:
        for proj, src in sources_with_project(board):
            cmd = [
                "arm-none-eabi-gcc", "-fsyntax-only", "-mthumb", "-std=gnu11",
                *_mcu_flags(board), "-DUSE_HAL_DRIVER", "-DUSE_FULL_LL_DRIVER",
                *WARN_FLAGS, *_includes(board, proj), str(src),
            ]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            found.extend(parse_warnings(proc.stderr))
    return sorted(set(found))


def parse_warnings(text: str) -> list[str]:
    return [normalise(line) for line in text.splitlines() if _WARN_RE.match(line.strip())]


def normalise(line: str) -> str:
    """Rewrite a compiler warning to repo-relative forward-slash form."""
    m = _WARN_RE.match(line.strip())
    if not m:
        return line.strip()
    raw = m.group("path").replace("\\", "/")
    root = str(REPO_ROOT).replace("\\", "/").rstrip("/") + "/"
    rel = raw[len(root):] if raw.startswith(root) else raw
    return f"{rel}:{m.group('line')}:{m.group('col')}: {m.group('msg')}"


def load_baseline() -> set[str]:
    if not BASELINE_PATH.is_file():
        return set()
    return {
        ln.strip()
        for ln in BASELINE_PATH.read_text(encoding="utf-8").splitlines()
        if ln.strip() and not ln.startswith("#")
    }


def update_baseline() -> int:
    warnings = collect_warnings()
    BASELINE_PATH.write_text(
        "# Accepted pre-existing warnings. Regenerate with:\n"
        "#   python vv/run_gate.py --update-baseline\n"
        + "\n".join(warnings) + "\n",
        encoding="utf-8",
    )
    return len(warnings)


def run() -> StageResult:
    current = set(collect_warnings())
    baseline = load_baseline()
    new = sorted(current - baseline)
    stale = sorted(baseline - current)

    items = [{"new": w} for w in new] + [{"stale": w} for w in stale]

    if new:
        return StageResult("static", "fail",
                           f"{len(new)} new warning(s)", items)
    if stale:
        return StageResult("static", "warn",
                           f"{len(stale)} stale baseline entr(y/ies); "
                           f"run --update-baseline", items)
    return StageResult("static", "pass",
                       f"no new warnings ({len(baseline)} baselined)", items)


run.stage_name = "static"
