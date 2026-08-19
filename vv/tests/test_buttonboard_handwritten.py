"""Tripwire: the ButtonBoard project's hand-written code lives INSIDE CubeMX
USER CODE fences, so a Generate Code is safe by construction.

History: on 2026-08-15 a regen destroyed the whole application loop because
main.c / hal_msp.c / it.c / main.h / hal_conf.h were hand-written with no
fences. On 2026-08-19 they were migrated into CubeMX-managed files with every
hand-written block placed in a fence, and a real regen was proven to change
nothing outside the fences (only the uncommitted bench HSI delta, which
deliberately lives in generated code). These assertions keep that true:
a hand-written marker drifting OUTSIDE a fence would be silently deleted by
the next regen, so it is a red gate here instead. See the project CLAUDE.md.
"""
import re
from vv.boards import REPO_ROOT

PROJ = REPO_ROOT / "STM32G0B1_Applciationprog" / "STM32G0_BUTTONBOARD_PROG"

# (file, marker) - every hand-written thing that MUST survive a regen.
FENCED = [
    ("Core/Src/main.c",              "VectorBase_Config();"),      # in USER CODE 1, before HAL_Init
    ("Core/Src/main.c",              "static void VectorBase_Config(void)"),
    ("Core/Src/main.c",              "AppLogic_Init(&sm, can_ok);"),
    ("Core/Src/main.c",              "I2CHost_Init();"),
    ("Core/Src/main.c",              "AppLogic_Run(&sm);"),        # loop body: in USER CODE 3
    ("Core/Src/main.c",              "Leds_Init();"),              # in MX_GPIO_Init_2
    ("Core/Src/main.c",              "sm.state == STATE_ERROR"),   # fast heartbeat
    ("Core/Src/stm32g0xx_hal_msp.c", "R11/R12"),                   # rationale comments
    ("Core/Src/stm32g0xx_hal_msp.c", "AF9, NOT AF6"),
    ("Core/Inc/main.h",              "extern I2C_HandleTypeDef   hi2c3;"),
    ("Core/Inc/main.h",              '#include "board_pins.h"'),
]

# Things the .ioc must GENERATE (they moved out of hand-written code on purpose).
GENERATED = [
    ("Core/Src/main.c",              "hi2c1.Init.OwnAddress1 = 162;"),   # 0x51 << 1
    ("Core/Src/stm32g0xx_hal_msp.c", "GPIO_InitStruct.Alternate = GPIO_AF9_I2C3;"),
    ("Core/Src/stm32g0xx_hal_msp.c", "HAL_NVIC_SetPriority(I2C1_IRQn, 1, 0);"),
    ("Core/Src/stm32g0xx_it.c",      "void I2C1_IRQHandler(void)"),
    ("Core/Src/stm32g0xx_hal_msp.c", "HAL_SYSCFG_StrobeDBattpinsConfig"),
]

FENCE_BEGIN = re.compile(r"/\* USER CODE BEGIN ([^*]+?) \*/")
FENCE_END = re.compile(r"/\* USER CODE END ([^*]+?) \*/")


def _fence_at(text: str, pos: int):
    """Name of the USER CODE fence enclosing offset `pos`, or None if unfenced."""
    open_fences = []
    for m in re.finditer(r"/\* USER CODE (BEGIN|END) ([^*]+?) \*/", text):
        if m.start() > pos:
            break
        if m.group(1) == "BEGIN":
            open_fences.append(m.group(2))
        elif open_fences and open_fences[-1] == m.group(2):
            open_fences.pop()
    return open_fences[-1] if open_fences else None


def test_handwritten_markers_present_and_inside_fences():
    for rel, marker in FENCED:
        text = (PROJ / rel).read_text(encoding="utf-8", errors="replace")
        pos = text.find(marker)
        assert pos >= 0, (
            f"{rel}: hand-written marker '{marker}' is missing - a regen has "
            f"probably deleted it. See the project CLAUDE.md.")
        fence = _fence_at(text, pos)
        assert fence is not None, (
            f"{rel}: '{marker}' sits OUTSIDE any USER CODE fence and will be "
            f"deleted by the next CubeMX Generate Code. Move it into a fence.")


def test_generated_decisions_come_from_ioc():
    """These used to be hand-written; now the .ioc generates them. If one goes
    missing the .ioc lost the setting (I2C1.OwnAddress, NVIC.I2C1_IRQn, AF9)."""
    for rel, marker in GENERATED:
        text = (PROJ / rel).read_text(encoding="utf-8", errors="replace")
        assert marker in text, (
            f"{rel}: generated line '{marker}' missing - the .ioc no longer "
            f"carries this setting, or the file was hand-edited outside a fence.")


def test_files_are_cubemx_managed():
    for rel in sorted({r for r, _ in FENCED}):
        text = (PROJ / rel).read_text(encoding="utf-8", errors="replace")
        assert "/* USER CODE BEGIN Header */" in text, (
            f"{rel} lacks the CubeMX header fence - it is not a CubeMX-managed "
            f"file and a regen would overwrite it wholesale.")


def test_vendored_hal_version_pinned():
    """Every review/HAL trace for this project is against FW_G0 1.6.2 = HAL 1.4.6.
    A regen with LibraryCopy=1 bumps this if the .ioc firmware pin drifts."""
    hal = (PROJ / "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c").read_text(errors="replace")
    ver = {k: int(v, 16) for k, v in re.findall(
        r"__STM32G0xx_HAL_VERSION_(MAIN|SUB1|SUB2)\s+\(0x([0-9A-Fa-f]+)U\)", hal)}
    assert (ver["MAIN"], ver["SUB1"], ver["SUB2"]) == (1, 4, 6), (
        f"vendored HAL is {ver['MAIN']}.{ver['SUB1']}.{ver['SUB2']}, expected 1.4.6 "
        f"(FW_G0 1.6.2). Restore Drivers/ from git and re-pin the .ioc.")


def test_ioc_regen_guards_intact():
    ioc = (PROJ / "STM32G0_BUTTONBOARD_PROG.ioc").read_text(errors="replace")
    assert "ProjectManager.NoMain=false" in ioc, "main.c is CubeMX-managed again; NoMain must be false"
    assert "ProjectManager.LastFirmware=false" in ioc, "ioc must not auto-migrate the HAL"
    assert "ProjectManager.FirmwarePackage=STM32Cube FW_G0 V1.6.2" in ioc, \
        "ioc firmware package must stay pinned to the vendored FW_G0 1.6.2"
    assert "ProjectManager.KeepUserCode=true" in ioc, "fences only work with KeepUserCode"
    # CubeMX's I2C panel takes the address in DECIMAL. 0x51 == 81. A regen once
    # produced OwnAddress=51 (== 0x33) after the panel was touched, which moved
    # the host port off its address; the generated-line check below caught it.
    assert "I2C1.OwnAddress=81" in ioc, "I2C1 slave address must be 81 decimal (= 0x51) in the ioc"
    assert "I2C1.OwnAddress=51" not in ioc, "I2C1.OwnAddress=51 is 0x33 - the CubeMX decimal-field trap; set 81"
    assert "NVIC.I2C1_IRQn=true" in ioc, "I2C1 IRQ must be generated from the ioc"


def test_board_pins_matches_generated_main_h():
    """The .ioc labels every pin, so generated main.h emits all *_Pin/*_GPIO_Port
    defines. board_pins.h keeps the same names (ifndef-guarded) as annotated
    documentation; the two must never disagree."""
    gen = (PROJ / "Core/Inc/main.h").read_text(errors="replace")
    bp = (PROJ / "Core/Inc/board_pins.h").read_text(errors="replace")
    pat = re.compile(r"#define\s+(\w+_(?:Pin|GPIO_Port))\s+(\S+)")
    g = {m.group(1): m.group(2) for m in pat.finditer(gen)}
    b = {m.group(1): m.group(2) for m in pat.finditer(bp)}
    assert g, "generated main.h has no pin defines - .ioc labels lost?"
    assert set(g) == set(b), f"pin define sets differ: only main.h={sorted(set(g)-set(b))} only board_pins={sorted(set(b)-set(g))}"
    mism = {k: (b[k], g[k]) for k in g if g[k] != b[k]}
    assert not mism, f"pin define VALUES differ between board_pins.h and generated main.h: {mism}"
