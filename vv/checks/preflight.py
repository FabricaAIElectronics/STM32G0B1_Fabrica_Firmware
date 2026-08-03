"""Stage 0 - assert the toolchain the later stages depend on is installed."""
import glob
import os
import re
import shutil
import subprocess
from pathlib import Path

from vv.result import StageResult

# Searched in order. Globs, so any installed version matches -- pinning a
# version here is what makes a gate that only runs on one machine.
CUBEIDE_GLOBS = (
    # Windows, ST's default installer location and Program Files variants
    r"C:/ST/STM32CubeIDE*/STM32CubeIDE/stm32cubeidec.exe",
    r"C:/Program Files/STMicroelectronics/STM32CubeIDE*/STM32CubeIDE/stm32cubeidec.exe",
    r"C:/Program Files (x86)/STMicroelectronics/STM32CubeIDE*/STM32CubeIDE/stm32cubeidec.exe",
    # Linux
    "/opt/st/stm32cubeide*/stm32cubeide",
    "/usr/local/stm32cubeide*/stm32cubeide",
    str(Path.home() / "st" / "stm32cubeide*" / "stm32cubeide"),
    # macOS
    "/Applications/STM32CubeIDE.app/Contents/MacOS/stm32cubeide",
)

REQUIRED_TOOLS = {
    "arm-none-eabi-gcc": "install the GNU Arm Embedded Toolchain and put it on PATH",
    "gcc": "Windows: install MSYS2, then `pacman -S mingw-w64-x86_64-gcc` and put "
           "C:\\msys64\\mingw64\\bin on PATH. Linux: apt install build-essential",
    "make": "Windows: `pacman -S make` in MSYS2 and put C:\\msys64\\usr\\bin on "
            "PATH AHEAD of GnuWin32. Linux: apt install make",
}


def find_cubeide() -> str | None:
    """Locate the CubeIDE console executable on any machine.

    Order: the CUBEIDE environment variable, then PATH, then the known install
    locations for Windows, Linux and macOS. Where a glob matches several
    versions the highest-sorting one wins, so a newer install is preferred.
    """
    override = os.environ.get("CUBEIDE")
    if override:
        # An explicit override that is wrong should be loud, not silently ignored.
        return override if os.path.isfile(override) else None

    for name in ("stm32cubeidec", "stm32cubeidec.exe", "stm32cubeide"):
        found = shutil.which(name)
        if found:
            return found

    for pattern in CUBEIDE_GLOBS:
        matches = sorted(glob.glob(pattern), reverse=True)
        for m in matches:
            if os.path.isfile(m):
                return m
    return None


def cubeide_version(path: str) -> str:
    """Best-effort version, taken from the install directory name."""
    m = re.search(r"STM32CubeIDE[_-]?(\d+(?:\.\d+)*)", path, re.IGNORECASE)
    if m:
        return m.group(1)
    m = re.search(r"stm32cubeide[_-]?(\d+(?:\.\d+)*)", path, re.IGNORECASE)
    return m.group(1) if m else "unknown"


def has_cantools() -> bool:
    try:
        import cantools  # noqa: F401
        return True
    except ImportError:
        return False


def _version(exe: str) -> str:
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30)
        return out.stdout.splitlines()[0].strip() if out.stdout else "unknown"
    except Exception:
        return "unknown"


def run() -> StageResult:
    items, missing = [], []

    for tool, hint in REQUIRED_TOOLS.items():
        path = shutil.which(tool)
        if path:
            items.append({"tool": tool, "path": path, "version": _version(tool)})
        else:
            missing.append(tool)
            items.append({"tool": tool, "install": hint})

    cubeide = find_cubeide()
    if cubeide:
        items.append({"tool": "stm32cubeidec", "path": cubeide,
                      "version": cubeide_version(cubeide)})
    else:
        missing.append("stm32cubeidec")
        items.append({"tool": "stm32cubeidec",
                      "install": "install STM32CubeIDE, or set the CUBEIDE "
                                 "environment variable to stm32cubeidec(.exe). "
                                 "Searched PATH and: "
                                 + "; ".join(CUBEIDE_GLOBS[:3]) + " ..."})

    if has_cantools():
        items.append({"tool": "cantools", "path": "python package"})
    else:
        missing.append("cantools")
        items.append({"tool": "cantools", "install": "pip install cantools"})

    # Report the environment; do NOT fail here. Each stage decides for itself
    # whether the tools it needs are present, and skips rather than failing if
    # not. Failing preflight would stop the run before the stages that CAN run
    # on this machine get a chance to.
    if missing:
        return StageResult(
            "preflight", "warn",
            f"{len(items) - len(missing)} present, missing: {', '.join(missing)}",
            items)
    return StageResult("preflight", "pass", f"{len(items)} tools present", items)


run.stage_name = "preflight"
