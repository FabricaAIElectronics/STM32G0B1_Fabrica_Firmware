"""Stage 2 - build and run the host unit tests via make."""
import re
import subprocess
from pathlib import Path

from vv.result import StageResult

UNIT_DIR = Path(__file__).resolve().parent
LAYOUTS_PATH = UNIT_DIR / "layouts.json"

_PASS_RE = re.compile(r"^TEST PASS (\S+)", re.MULTILINE)
_FAIL_RE = re.compile(r"^TEST FAIL (\S+)", re.MULTILINE)


def parse_output(text: str) -> dict:
    failures = _FAIL_RE.findall(text)
    return {
        "passed": len(_PASS_RE.findall(text)),
        "failed": len(failures),
        "failures": failures,
    }


def run_make() -> tuple[int, str]:
    proc = subprocess.run(["make", "-C", str(UNIT_DIR), "all"],
                          capture_output=True, text=True, timeout=600)
    return proc.returncode, proc.stdout + proc.stderr


def run() -> StageResult:
    code, output = run_make()
    summary = parse_output(output)
    items = [{"passed": summary["passed"], "failed": summary["failed"]}]
    items += [{"failure": f} for f in summary["failures"]]

    if summary["failed"] or (code != 0 and summary["passed"] == 0):
        detail = ", ".join(summary["failures"]) or f"make exited {code}"
        return StageResult("unit", "fail", f"failing: {detail}", items)
    return StageResult("unit", "pass", f"{summary['passed']} assertions passed", items)


run.stage_name = "unit"
