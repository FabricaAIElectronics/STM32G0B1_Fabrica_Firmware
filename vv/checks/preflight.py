"""Stage 0 - assert the toolchain the later stages depend on is installed."""
import os
import shutil
import subprocess

from vv.result import StageResult

DEFAULT_CUBEIDE = r"C:/ST/STM32CubeIDE_1.18.0/STM32CubeIDE/stm32cubeidec.exe"

REQUIRED_TOOLS = {
    "arm-none-eabi-gcc": "install the GNU Arm Embedded Toolchain and put it on PATH",
    "gcc": "install MSYS2 then: pacman -S mingw-w64-x86_64-gcc",
    "make": "install MSYS2 then: pacman -S make",
}


def find_cubeide() -> str | None:
    """Return the CubeIDE console executable path, or None if not found."""
    candidate = os.environ.get("CUBEIDE", DEFAULT_CUBEIDE)
    return candidate if os.path.isfile(candidate) else None


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
        items.append({"tool": "stm32cubeidec", "path": cubeide})
    else:
        missing.append("stm32cubeidec")
        items.append({"tool": "stm32cubeidec",
                      "install": f"install STM32CubeIDE, or set CUBEIDE=<path> "
                                 f"(looked for {DEFAULT_CUBEIDE})"})

    if has_cantools():
        items.append({"tool": "cantools", "path": "python package"})
    else:
        missing.append("cantools")
        items.append({"tool": "cantools", "install": "pip install cantools"})

    if missing:
        return StageResult("preflight", "fail",
                           f"missing: {', '.join(missing)}", items)
    return StageResult("preflight", "pass", f"{len(items)} tools present", items)


run.stage_name = "preflight"
