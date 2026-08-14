"""Board metadata registry - the single declaration every gate stage reads."""
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

FLASH_512K = 512 * 1024
RESERVE_G0B1 = 12 * 1024
RESERVE_F303 = 14 * 1024


@dataclass(frozen=True)
class Board:
    id: str
    name: str
    mcu: str
    app_dir: str
    app_eclipse: str
    boot_dir: str
    boot_eclipse: str
    dbc: str | None
    headers: tuple[str, ...]
    blt_rx: int
    blt_tx: int
    bitrate: int
    extended: bool
    boot_reserved_bytes: int
    app_origin: int
    flash_total_bytes: int
    address_plan_exempt: bool
    in_bus_doc: bool


_APP_G0 = "STM32G0B1_Applciationprog"
_BOOT_G0 = "STM32G0B1_Bootloader"

BOARDS: list[Board] = [
    Board(
        id="kincodrive",
        name="KincoDrive",
        mcu="STM32G0B1RET6",
        app_dir=f"{_APP_G0}/KincoDrive_ControlModule_V5_4",
        app_eclipse="Actuation_IO_Distribution_Board_Embedded",
        boot_dir=f"{_BOOT_G0}/G0B1_KincoDrive_Boot",
        boot_eclipse="G0B1_KincoDrive_Boot",
        dbc=f"{_APP_G0}/KincoDrive_ControlModule_V5_4/KincoDrive_ControlModule.dbc",
        headers=(f"{_APP_G0}/KincoDrive_ControlModule_V5_4/Core/Inc/CAN_Handler.h",),
        blt_rx=0x101,
        blt_tx=0x102,
        bitrate=500000,
        extended=False,
        boot_reserved_bytes=RESERVE_G0B1,
        app_origin=0x08003000,
        flash_total_bytes=FLASH_512K,
        address_plan_exempt=False,
        in_bus_doc=True,
    ),
    Board(
        id="powerstage",
        name="PowerStage",
        mcu="STM32G0B1RET6",
        app_dir=f"{_APP_G0}/PowerStage",
        app_eclipse="PowerStage",
        boot_dir=f"{_BOOT_G0}/G0B1_PowerStage_Boot",
        boot_eclipse="G0B1_PowerStage_Boot",
        dbc=f"{_APP_G0}/PowerStage/PowerStage.dbc",
        headers=(f"{_APP_G0}/PowerStage/Core/Inc/can_operation.h",),
        blt_rx=0x130,
        blt_tx=0x131,
        bitrate=500000,
        extended=False,
        boot_reserved_bytes=RESERVE_G0B1,
        app_origin=0x08003000,
        flash_total_bytes=FLASH_512K,
        address_plan_exempt=False,
        in_bus_doc=True,
    ),
    Board(
        id="leddriver",
        name="LEDDriver",
        mcu="STM32G0B1RET6",
        app_dir=f"{_APP_G0}/STM32G0_LEDDRIVER_PROG",
        app_eclipse="STM32G0_LEDDRIVER_PROG",
        boot_dir=f"{_BOOT_G0}/G0B1_LEDDriver_Boot",
        boot_eclipse="G0B1_LEDDriver_Boot",
        dbc=f"{_APP_G0}/STM32G0_LEDDRIVER_PROG/LEDDriver.dbc",
        headers=(f"{_APP_G0}/STM32G0_LEDDRIVER_PROG/Core/Inc/can_operation.h",),
        blt_rx=0x160,
        blt_tx=0x161,
        bitrate=500000,
        extended=False,
        boot_reserved_bytes=RESERVE_G0B1,
        app_origin=0x08003000,
        flash_total_bytes=FLASH_512K,
        address_plan_exempt=False,
        in_bus_doc=True,
    ),
    Board(
        id="buttonboard",
        name="ButtonBoard",
        mcu="STM32G0B1RET6",
        app_dir=f"{_APP_G0}/STM32G0_BUTTONBOARD_PROG",
        app_eclipse="STM32G0_BUTTONBOARD_PROG",
        boot_dir=f"{_BOOT_G0}/G0B1_ButtonBoard_Boot",
        boot_eclipse="G0B1_ButtonBoard_Boot",
        dbc=f"{_APP_G0}/STM32G0_BUTTONBOARD_PROG/ButtonBoard.dbc",
        headers=(f"{_APP_G0}/STM32G0_BUTTONBOARD_PROG/Core/Inc/can_operation.h",),
        blt_rx=0x780,
        blt_tx=0x781,
        bitrate=500000,
        extended=False,
        boot_reserved_bytes=RESERVE_G0B1,
        app_origin=0x08003000,
        flash_total_bytes=FLASH_512K,
        address_plan_exempt=False,
        in_bus_doc=True,
    ),
    # The knob predates the 0x101-0x17F address plan. Its ids sit in CANopen SDO
    # space, it has no DBC, and it is absent from Docs/CAN_Bus.md. Another team
    # owns that; the gate records it as a warning, never a failure.
    Board(
        id="knob",
        name="Operation Knob",
        mcu="STM32F303RET6",
        app_dir="STM32F303_Applciationprog/Fabrica_STM32F3_Prog",
        app_eclipse="Fabrica_STM32F3_Prog",
        boot_dir="STM32F303_Bootloader/Fabrica_STM32F3RE_Boot",
        boot_eclipse="Fabrica_STM32F3RE_Boot",
        dbc="STM32F303_Applciationprog/Fabrica_STM32F3_Prog/Knob.dbc",
        headers=("STM32F303_Applciationprog/Fabrica_STM32F3_Prog/Core/Inc/can_operation.h",),
        blt_rx=0x667,
        blt_tx=0x7E1,
        bitrate=500000,
        extended=False,
        boot_reserved_bytes=RESERVE_F303,
        app_origin=0x08003800,
        flash_total_bytes=FLASH_512K,
        address_plan_exempt=True,
        in_bus_doc=False,
    ),
]


def board_by_id(bid: str) -> Board:
    """Return the board with this id, or raise KeyError."""
    for b in BOARDS:
        if b.id == bid:
            return b
    raise KeyError(f"unknown board id: {bid!r}")
