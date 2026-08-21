"""Which command signals are configuration, and where each one reads back.

The DBC says what a command frame looks like. It does not say whether a board
echoes what you wrote, nor where. That mapping is here, and it is hand-written
because it cannot be derived: the names do not correspond
(``Fan_Duty`` -> ``Fan_Duty_State`` -> ``Cfg_Fan_Def_Duty``), and some
plausible-looking pairs are not echoes at all.

Two readback surfaces, deliberately distinct
--------------------------------------------
``live`` is what the board is doing now. ``stored`` is what it has in EEPROM
and will come back to after a power cycle. They are allowed to disagree - that
is the normal state after a write and before a save - so a parameter editor
that shows only one of them either hides unsaved work or reports a fault
against a board behaving correctly.

``fabrica.verify`` already learned this the expensive way on KincoDrive:
``Cmd_OC_Threshold`` -> ``Bcast_Config_A`` looks like an echo and is not.
``Bcast_Config_A`` packs the EEPROM-cached config, while the command sets the
live threshold and is RAM-only until a save. Pairing them as live produced a
confident "the command is not reaching the board" against correct firmware.
Here that pair is recorded as ``stored`` only, and ``live`` is None.

Things that are measured rather than echoed are also None: KincoDrive's
``Bcast_Fans`` carries fan *tachometer* percent, so it never equals a commanded
duty and must not be compared against one.

Persist encodings are per board and do not agree
------------------------------------------------
The value 0 means "load factory defaults and apply" on KincoDrive and "no-op"
on PowerStage. A shared "send zero to reset" would wipe one board's config and
do nothing on another's. Each board's save / load-defaults frame is therefore
spelled out in :class:`PersistOps` rather than assumed.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Echo:
    """Where a written signal reads back.

    ``signal`` is the 1:1 case. ``bits`` is for a command that packs a mask
    which the telemetry reports as separate per-bit signals - ButtonBoard's
    ``Led_Mask`` against ``Led_1..Led_6``. Bits are listed LSB first.
    """
    message: str
    signal: str | None = None
    bits: tuple[str, ...] = ()

    def __post_init__(self):
        if bool(self.signal) == bool(self.bits):
            raise ValueError(
                f"{self.message}: give exactly one of signal or bits")


@dataclass(frozen=True)
class Param:
    signal: str
    live: Echo | None = None
    stored: Echo | None = None
    #: Non-None when writing this physically changes the world. The text is
    #: shown in the confirmation prompt, so it says what will happen.
    actuates: str | None = None
    note: str | None = None


@dataclass(frozen=True)
class CommandGroup:
    """One DBC command message: the unit a write is sent in.

    A CAN frame carries every signal in its message, so editing one parameter
    still transmits its siblings. The editor seeds the whole group from
    readback before letting anything be changed, otherwise a single-field edit
    would quietly zero the rest of the frame.
    """
    message: str
    params: tuple[Param, ...]
    note: str | None = None


@dataclass(frozen=True)
class PersistOps:
    """The board's save / load-defaults frames, as signal-value dicts."""
    message: str
    save: dict
    load_defaults: dict | None = None


@dataclass(frozen=True)
class BoardParams:
    groups: tuple[CommandGroup, ...] = ()
    persist: PersistOps | None = None
    #: Command messages that are momentary actions, not configuration. Listed
    #: so the "is every command message accounted for" check stays honest
    #: instead of silently ignoring what it does not model.
    actions: tuple[str, ...] = ()


# ---------------------------------------------------------------------------
# KincoDrive
# ---------------------------------------------------------------------------

_KINCODRIVE = BoardParams(
    groups=(
        CommandGroup(
            message="Cmd_HS_Power",
            note="Setting a rail bit also clears that channel's latched OC error.",
            params=(
                Param("HS_DR_OnOff", live=Echo("Bcast_GPIO", "HS_DR_Enable"),
                      actuates="switches the Drive high-side rail"),
                Param("HS_E_OnOff", live=Echo("Bcast_GPIO", "HS_E_Enable"),
                      actuates="switches the Extruder high-side rail"),
                Param("HS_SC_OnOff", live=Echo("Bcast_GPIO", "HS_SC_Enable"),
                      actuates="switches the Scrubbing high-side rail"),
                Param("VBUCK_OnOff", live=Echo("Bcast_GPIO", "VBUCK_Enable"),
                      actuates="switches the 12V buck"),
            ),
        ),
        CommandGroup(
            message="Cmd_Fan_PWM",
            note=("Bcast_Fans is tachometer percent, a measured value, so there "
                  "is no live echo to compare a commanded duty against."),
            params=(
                Param("Fan_DR_Speed",
                      stored=Echo("Bcast_Config_B", "Cfg_Fan_DR_Default")),
                Param("Fan_EP_Speed",
                      stored=Echo("Bcast_Config_B", "Cfg_Fan_EP_Default")),
                Param("Fan_EH_Speed",
                      stored=Echo("Bcast_Config_B", "Cfg_Fan_EH_Default")),
                Param("Fan_ST_Speed",
                      stored=Echo("Bcast_Config_B", "Cfg_Fan_ST_Default")),
                Param("Fan_SF_Speed",
                      note="no stored default is broadcast for the SF fan"),
            ),
        ),
        CommandGroup(
            message="Cmd_OC_Threshold",
            note=("RAM-only until a save; the stored column moves only after "
                  "CMD_EEPROM."),
            params=(
                Param("OC_DR_mA", stored=Echo("Bcast_Config_A", "Cfg_OC_DR_mA")),
                Param("OC_EXT_mA", stored=Echo("Bcast_Config_A", "Cfg_OC_EXT_mA")),
                Param("OC_SC_mA", stored=Echo("Bcast_Config_A", "Cfg_OC_SC_mA")),
            ),
        ),
        CommandGroup(
            message="Cmd_UV_Threshold",
            params=(
                Param("UV_24V_mV", stored=Echo("Bcast_Config_B", "Cfg_UV_24V_mV")),
                Param("UV_12V_mV", stored=Echo("Bcast_Config_B", "Cfg_UV_12V_mV")),
            ),
        ),
    ),
    persist=PersistOps(
        message="Cmd_EEPROM",
        save={"EEPROM_Action": 1},
        load_defaults={"EEPROM_Action": 0},
    ),
)


# ---------------------------------------------------------------------------
# PowerStage
# ---------------------------------------------------------------------------

_POWERSTAGE = BoardParams(
    groups=(
        CommandGroup(
            message="CMD_FAN",
            params=(
                Param("Fan_Mode", live=Echo("BCAST_FAN", "Fan_Mode_State"),
                      stored=Echo("BCAST_EEPROM", "Cfg_Fan_Def_Mode")),
                Param("Fan_Duty", live=Echo("BCAST_FAN", "Fan_Duty_State"),
                      stored=Echo("BCAST_EEPROM", "Cfg_Fan_Def_Duty")),
                Param("Fan_Min_Duty", live=Echo("BCAST_FAN", "Fan_Min_Duty_State"),
                      stored=Echo("BCAST_EEPROM", "Cfg_Fan_Min_Duty")),
                Param("Fan_Auto_On_Temp",
                      live=Echo("BCAST_FAN", "Fan_Auto_On_Temp_State"),
                      stored=Echo("BCAST_EEPROM", "Cfg_Auto_On_Temp")),
                Param("Fan_Auto_Off_Temp",
                      live=Echo("BCAST_FAN", "Fan_Auto_Off_Temp_State"),
                      stored=Echo("BCAST_EEPROM", "Cfg_Auto_Off_Temp")),
            ),
        ),
        CommandGroup(
            message="CMD_HS",
            note="RAIL_SBC has no MCU-driven enable line; the board ignores that bit.",
            params=(
                Param("HS_Cmd_AUX", live=Echo("BCAST_HS_STATE", "HS_Enable_AUX"),
                      stored=Echo("BCAST_EEPROM", "Cfg_HS_Def_AUX"),
                      actuates="switches the AUX rail"),
                Param("HS_Cmd_LED", live=Echo("BCAST_HS_STATE", "HS_Enable_LED"),
                      stored=Echo("BCAST_EEPROM", "Cfg_HS_Def_LED"),
                      actuates="switches the LED rail"),
                Param("HS_Cmd_DRIVE", live=Echo("BCAST_HS_STATE", "HS_Enable_DRIVE"),
                      stored=Echo("BCAST_EEPROM", "Cfg_HS_Def_DRIVE"),
                      actuates="switches the DRIVE rail"),
                Param("HS_Cmd_CAP", live=Echo("BCAST_HS_STATE", "HS_Enable_CAP"),
                      stored=Echo("BCAST_EEPROM", "Cfg_HS_Def_CAP"),
                      actuates="switches the CAP rail"),
                Param("HS_Cmd_SBC", live=Echo("BCAST_HS_STATE", "HS_Enable_SBC"),
                      stored=Echo("BCAST_EEPROM", "Cfg_HS_Def_SBC"),
                      note="ignored by the firmware: no MCU-driven EN line"),
            ),
        ),
        CommandGroup(
            message="CMD_OC",
            note="BCAST_OC_CFG_A reports the ACTIVE thresholds, so this one is live.",
            params=(
                Param("OC_Thr_AUX_mA",
                      live=Echo("BCAST_OC_CFG_A", "OC_Thr_AUX_mA")),
                Param("OC_Thr_LED_mA",
                      live=Echo("BCAST_OC_CFG_A", "OC_Thr_LED_mA")),
                Param("OC_Thr_DRIVE_mA",
                      live=Echo("BCAST_OC_CFG_A", "OC_Thr_DRIVE_mA")),
                Param("OC_Thr_CAP_mA",
                      live=Echo("BCAST_OC_CFG_A", "OC_Thr_CAP_mA")),
            ),
        ),
        CommandGroup(
            message="CMD_UV",
            params=(
                Param("UV_V24_mV", live=Echo("BCAST_UV", "UV_Active_V24_mV")),
                Param("UV_VCAP_mV", live=Echo("BCAST_UV", "UV_Active_VCAP_mV")),
                Param("UV_V12_mV", live=Echo("BCAST_UV", "UV_Active_V12_mV")),
            ),
        ),
        CommandGroup(
            message="CMD_CTRL",
            params=(
                Param("V_LED_PWR_Cmd",
                      live=Echo("BCAST_IO_STATUS", "V_LED_PWR_State"),
                      actuates="switches the LED power rail"),
                Param("CAN_Relay_Enable",
                      live=Echo("BCAST_IO_STATUS", "CAN_Relay_State"),
                      actuates="opens or closes the CAN relay, which can cut "
                               "this board off the bus"),
            ),
        ),
        CommandGroup(
            message="CMD_PAGE_DWELL",
            note="OLED page timing; the board broadcasts no readback for it.",
            params=(
                Param("Page_Dwell_Overview"),
                Param("Page_Dwell_RailStatus"),
                Param("Page_Dwell_FaultDetail"),
            ),
        ),
        CommandGroup(
            message="CMD_BAT_CFG",
            note=("BCAST_BATTERY_CFG carries the static 6S pack constants, not "
                  "this threshold, so there is nothing to compare against."),
            params=(Param("Bat_Low_SOC_Threshold_pct"),),
        ),
    ),
    persist=PersistOps(
        message="CMD_EEPROM",
        save={"EEPROM_Cmd": 1},
        load_defaults={"EEPROM_Cmd": 2},
    ),
    actions=("CMD_OC_RESET",),
)


# ---------------------------------------------------------------------------
# LEDDriver
# ---------------------------------------------------------------------------

_LEDDRIVER = BoardParams(
    groups=(
        CommandGroup(
            message="CMD_LIGHTSET",
            note=("BCAST_LIGHTSTATUS reports the APPLIED duty. While the board "
                  "is in STATE_ERROR (undervoltage) it sheds the LED outputs by "
                  "design and ignores this command, so a live mismatch there is "
                  "correct behaviour, not a fault."),
            params=(
                Param("PWM_Channel0",
                      live=Echo("BCAST_LIGHTSTATUS", "PWM_Ch0_State"),
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_PWM_Ch0"),
                      actuates="drives LED channel 0"),
                Param("PWM_Channel1",
                      live=Echo("BCAST_LIGHTSTATUS", "PWM_Ch1_State"),
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_PWM_Ch1"),
                      actuates="drives LED channel 1"),
                Param("PWM_Channel2",
                      live=Echo("BCAST_LIGHTSTATUS", "PWM_Ch2_State"),
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_PWM_Ch2"),
                      actuates="drives LED channel 2"),
            ),
        ),
        CommandGroup(
            message="CMD_VOLTAGESET",
            params=(
                Param("UV_24V_mV",
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_UV_24V_mV")),
                Param("UV_17V5_mV",
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_UV_17V5_mV")),
                Param("Buck_Mode",
                      stored=Echo("BCAST_EEPROMDATA", "Cfg_Buck_Mode"),
                      actuates="in ON or AUTO this enables the buck converter"),
            ),
        ),
    ),
    persist=PersistOps(
        message="CMD_EEPROMSET",
        save={"EEPROM_Save_Flag": 1, "EEPROM_LoadDefault_Flag": 0},
        load_defaults={"EEPROM_Save_Flag": 0, "EEPROM_LoadDefault_Flag": 1},
    ),
)


# ---------------------------------------------------------------------------
# ButtonBoard
# ---------------------------------------------------------------------------

_BUTTONBOARD = BoardParams(
    groups=(
        CommandGroup(
            message="CMD_LED",
            params=(
                Param("Led_Mask",
                      live=Echo("LEDSTATE", bits=("Led_1", "Led_2", "Led_3",
                                                  "Led_4", "Led_5", "Led_6")),
                      stored=Echo("EEPROMDATA", "Default_Led_Mask"),
                      actuates="lights the illuminated buttons"),
                Param("Led_Int_Mask",
                      live=Echo("LEDSTATE", bits=("Led_Int_1", "Led_Int_2",
                                                  "Led_Int_3", "Led_Int_4")),
                      stored=Echo("EEPROMDATA", "Default_Led_Int_Mask"),
                      actuates="lights the internal LEDs"),
            ),
        ),
        CommandGroup(
            message="CMD_BUFFER",
            note=("Firmware guarantees the two buffer banks are never both "
                  "enabled. LEDSTATE reports the pins read back, not the "
                  "request, so a disagreement is visible."),
            params=(
                Param("Led_Source", live=Echo("LEDSTATE", "Led_Source"),
                      stored=Echo("EEPROMDATA", "Default_Led_Source"),
                      actuates="hands the LED nets between the J1 direct path "
                               "and the STM32"),
            ),
        ),
    ),
    persist=PersistOps(
        message="CMD_EEPROM",
        save={"Command": 0x01},
        load_defaults={"Command": 0x02},
    ),
    actions=("CMD_ENCODER",),
)


# ---------------------------------------------------------------------------
# Operation Knob - no EEPROM command exists in its protocol.
# ---------------------------------------------------------------------------

_KNOB = BoardParams(
    groups=(
        CommandGroup(
            message="KNOBCOMMAND",
            note="The knob has no CMD_EEPROM: nothing it accepts is persistable.",
            params=(Param("Com_Enable_Mask", live=Echo("KNOBSTATE", "Com_State")),),
        ),
    ),
)


BOARD_PARAMS: dict[str, BoardParams] = {
    "kincodrive": _KINCODRIVE,
    "powerstage": _POWERSTAGE,
    "leddriver": _LEDDRIVER,
    "buttonboard": _BUTTONBOARD,
    "knob": _KNOB,
}


# ---------------------------------------------------------------------------
# Lookup helpers
# ---------------------------------------------------------------------------


def for_board(board_id: str) -> BoardParams:
    """Parameters for a board; an empty set for one that is not modelled."""
    return BOARD_PARAMS.get(board_id, BoardParams())


def group(board_id: str, message: str) -> CommandGroup:
    for g in for_board(board_id).groups:
        if g.message == message:
            return g
    known = ", ".join(g.message for g in for_board(board_id).groups) or "(none)"
    raise KeyError(
        f"{board_id} has no configurable message {message!r}; has: {known}")


def find(board_id: str, signal: str) -> tuple[CommandGroup, Param]:
    for g in for_board(board_id).groups:
        for p in g.params:
            if p.signal == signal:
                return g, p
    raise KeyError(f"{board_id} has no configurable signal {signal!r}")


def signals(board_id: str) -> list[str]:
    return [p.signal for g in for_board(board_id).groups for p in g.params]


def actuating(board_id: str) -> list[Param]:
    return [p for g in for_board(board_id).groups for p in g.params if p.actuates]


def readback_messages(board_id: str) -> set[str]:
    """Every telemetry message the config screen needs to observe."""
    out = set()
    for g in for_board(board_id).groups:
        for p in g.params:
            for echo in (p.live, p.stored):
                if echo is not None:
                    out.add(echo.message)
    return out


def validate(board_id: str, db) -> list[str]:
    """Check this board's map against its DBC. Returns human-readable problems.

    Run by the V&V conformance stage, so a signal renamed in a DBC cannot leave
    a dangling entry here to be discovered on a bench instead.
    """
    problems: list[str] = []
    if db is None:
        return problems
    by_name = {m.name: {s.name for s in m.signals} for m in db.messages}
    bp = for_board(board_id)

    def check(message: str, signal: str, where: str) -> None:
        if message not in by_name:
            problems.append(f"{board_id}: {where} names message {message!r}, "
                            f"absent from the DBC")
        elif signal not in by_name[message]:
            problems.append(f"{board_id}: {where} names {message}.{signal!r}, "
                            f"absent from the DBC")

    for g in bp.groups:
        for p in g.params:
            check(g.message, p.signal, "command")
            for kind, echo in (("live", p.live), ("stored", p.stored)):
                if echo is None:
                    continue
                for name in ((echo.signal,) if echo.signal else echo.bits):
                    check(echo.message, name, f"{p.signal} {kind} echo")

    if bp.persist is not None:
        merged = dict(bp.persist.save)
        merged.update(bp.persist.load_defaults or {})
        for signal in merged:
            check(bp.persist.message, signal, "persist")

    return problems
