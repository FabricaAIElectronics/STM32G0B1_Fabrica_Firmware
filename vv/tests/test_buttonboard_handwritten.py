"""Tripwire: the ButtonBoard project's C is hand-written, not CubeMX-managed.

A CubeMX "Generate Code" on STM32G0_BUTTONBOARD_PROG.ioc overwrites main.c /
hal_msp.c / it.c / main.h / hal_conf.h with boilerplate (they carry no
USER CODE fences) and silently bumps the vendored HAL. That happened once
(2026-08-15) and cost the whole application loop; docs alone were not enough
to prevent it. These assertions turn a regen into a red gate instead of a
silent surprise. See STM32G0_BUTTONBOARD_PROG/CLAUDE.md for the recovery.
"""
import re
from vv.boards import REPO_ROOT

PROJ = REPO_ROOT / "STM32G0B1_Applciationprog" / "STM32G0_BUTTONBOARD_PROG"

# (file, marker that only the hand-written version contains)
HANDWRITTEN = [
    ("Core/Src/main.c",             "VectorBase_Config"),   # app entry, never in generated main
    ("Core/Src/main.c",             "AppLogic_Init("),
    ("Core/Src/main.c",             "I2CHost_Init("),
    ("Core/Src/stm32g0xx_hal_msp.c", "GPIO_AF9_I2C3"),      # the AF number a regen would keep,
                                                            # but generated msp lacks the rationale
    ("Core/Src/stm32g0xx_hal_msp.c", "R11/R12"),            # board-rationale comment, hand-written only
    ("Core/Src/stm32g0xx_it.c",     "I2C1_IRQHandler"),
    ("Core/Inc/main.h",             "hi2c3"),
]

# CubeMX writes this exact banner into every file it generates.
CUBEMX_BANNER = "/* USER CODE BEGIN Header */"


def test_handwritten_files_still_handwritten():
    for rel, marker in HANDWRITTEN:
        text = (PROJ / rel).read_text(encoding="utf-8", errors="replace")
        assert marker in text, (
            f"{rel} no longer contains '{marker}' - a CubeMX Generate Code has "
            f"probably overwritten it. Restore from git; see the project CLAUDE.md.")


def test_no_cubemx_banner_in_handwritten_files():
    for rel in sorted({r for r, _ in HANDWRITTEN}):
        text = (PROJ / rel).read_text(encoding="utf-8", errors="replace")
        assert CUBEMX_BANNER not in text, (
            f"{rel} starts with the CubeMX generated-file banner - it has been "
            f"regenerated. Restore from git; see the project CLAUDE.md.")


def test_vendored_hal_version_pinned():
    """Every review/HAL trace for this project is against FW_G0 1.6.2 = HAL 1.4.6.
    A regen with LibraryCopy=1 bumps this silently."""
    hal = (PROJ / "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c").read_text(errors="replace")
    ver = {k: int(v, 16) for k, v in re.findall(
        r"__STM32G0xx_HAL_VERSION_(MAIN|SUB1|SUB2)\s+\(0x([0-9A-Fa-f]+)U\)", hal)}
    assert (ver["MAIN"], ver["SUB1"], ver["SUB2"]) == (1, 4, 6), (
        f"vendored HAL is {ver['MAIN']}.{ver['SUB1']}.{ver['SUB2']}, expected 1.4.6 "
        f"(FW_G0 1.6.2). A CubeMX regen bumped it - restore Drivers/ from git.")


def test_ioc_regen_guards_intact():
    ioc = (PROJ / "STM32G0_BUTTONBOARD_PROG.ioc").read_text(errors="replace")
    assert "ProjectManager.NoMain=true" in ioc, "ioc must not generate main.c"
    assert "ProjectManager.LastFirmware=false" in ioc, "ioc must not auto-migrate the HAL"
    assert "ProjectManager.FirmwarePackage=STM32Cube FW_G0 V1.6.2" in ioc, \
        "ioc firmware package must stay pinned to the vendored FW_G0 1.6.2"
