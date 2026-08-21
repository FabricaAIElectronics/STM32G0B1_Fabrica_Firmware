"""Read, write, verify and persist board configuration over CAN.

Built on :mod:`fabrica.params`, which says which command signals are
configuration and where each reads back, and :mod:`fabrica.canbus`, which does
the DBC encode and the framing.

Why a write is not a write until it is read back
------------------------------------------------
These commands are fire-and-forget CAN frames. Nothing acknowledges them. The
only evidence a parameter landed is the board's own telemetry, so every write
here is followed by an observation window and a comparison.

The window has to be generous. LEDDriver and ButtonBoard rotate their stored
config broadcast through a three-phase cycle at roughly 300 ms and 500 ms a
phase, so ``BCAST_EEPROMDATA`` comes round about every 900 ms to 1.5 s. A 500 ms
timeout would report "no readback" against a board that is answering correctly,
which is the same class of mistake as pairing a command with a measured value.

Live and stored are separate verdicts
-------------------------------------
A parameter that has been written but not saved is ``live`` matching and
``stored`` stale. That is not a fault, it is unsaved work, and the result
carries both so the caller can show it as such rather than picking one and
being wrong half the time.
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from fabrica import canbus, params

#: Long enough for the slowest rotation (ButtonBoard and LEDDriver cycle their
#: stored-config broadcast through three phases) plus a margin.
DEFAULT_READ_TIMEOUT = 3.0

#: A save has to be written to EEPROM and then reflected in the next rotation
#: of the config broadcast, so it needs longer than a plain parameter write.
DEFAULT_PERSIST_TIMEOUT = 5.0

MATCH = "match"
DIFFER = "differ"
UNKNOWN = "unknown"      # no frame carrying this readback arrived
NO_ECHO = "no-echo"      # the board broadcasts no readback for this parameter


class ConfigError(RuntimeError):
    """Configuration could not be read, written or persisted."""


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------


@dataclass
class ParamValue:
    signal: str
    live: Any = None
    stored: Any = None
    live_seen: bool = False
    stored_seen: bool = False

    def readback(self, prefer: str = "live") -> Any:
        """Best available current value.

        ``live`` first by default: it is what the board is doing. Falls back to
        ``stored`` for the parameters whose only readback is the EEPROM echo -
        KincoDrive's OC thresholds, every LEDDriver voltage setting.
        """
        if prefer == "live" and self.live_seen:
            return self.live
        if self.stored_seen:
            return self.stored
        if self.live_seen:
            return self.live
        return None


@dataclass
class BoardState:
    board_id: str
    values: dict[str, ParamValue] = field(default_factory=dict)
    #: Message names that were expected but never arrived, for honest reporting.
    missing: tuple[str, ...] = ()

    def get(self, signal: str) -> ParamValue | None:
        return self.values.get(signal)

    def as_dict(self, prefer: str = "live") -> dict:
        return {name: v.readback(prefer) for name, v in self.values.items()
                if v.live_seen or v.stored_seen}


def _decoded_by_message(db, monitor: canbus.Monitor) -> dict[str, dict]:
    """Latest decoded signal dict per message name."""
    out: dict[str, dict] = {}
    for frame in monitor.snapshot():
        if frame.decoded and frame.name:
            out[frame.name] = frame.signals
    return out


def _bits_to_mask(decoded: dict, bits: tuple[str, ...]) -> int | None:
    """Recombine per-bit telemetry signals into the mask the command carries."""
    mask = 0
    for index, name in enumerate(bits):
        if name not in decoded:
            return None
        if int(decoded[name]):
            mask |= 1 << index
    return mask


def _extract(decoded_by_msg: dict[str, dict], echo) -> tuple[bool, Any]:
    if echo is None:
        return False, None
    decoded = decoded_by_msg.get(echo.message)
    if decoded is None:
        return False, None
    if echo.bits:
        mask = _bits_to_mask(decoded, echo.bits)
        return (mask is not None), mask
    if echo.signal not in decoded:
        return False, None
    return True, decoded[echo.signal]


def collect(bus, db, board_id: str,
            timeout: float = DEFAULT_READ_TIMEOUT) -> BoardState:
    """Listen for the readback messages this board's parameters need.

    Returns as soon as every expected message has been seen once, so a healthy
    board does not cost the full timeout. Reports what never arrived rather
    than leaving a caller to infer it from missing values.
    """
    wanted = params.readback_messages(board_id)
    monitor = canbus.Monitor(db)
    seen: set[str] = set()
    deadline = time.time() + timeout

    while time.time() < deadline and seen != wanted:
        msg = bus.recv(timeout=max(0.0, deadline - time.time()))
        if msg is None:
            continue
        frame = monitor.observe(msg.arbitration_id, bytes(msg.data),
                                getattr(msg, "timestamp", 0.0))
        if frame.decoded and frame.name in wanted:
            seen.add(frame.name)

    decoded_by_msg = _decoded_by_message(db, monitor)
    state = BoardState(board_id, missing=tuple(sorted(wanted - seen)))
    for grp in params.for_board(board_id).groups:
        for p in grp.params:
            live_seen, live = _extract(decoded_by_msg, p.live)
            stored_seen, stored = _extract(decoded_by_msg, p.stored)
            state.values[p.signal] = ParamValue(p.signal, live, stored,
                                                live_seen, stored_seen)
    return state


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------


def _numeric(value):
    """Plain number for a value that may be an enumerated NamedSignalValue.

    A seeded value comes from *telemetry*, and the telemetry signal's choice
    names are not required to match the command signal's. Encoding
    ``HS_Enable_DRIVE``'s "DISABLE" into ``HS_Cmd_DRIVE`` raises a bare KeyError
    out of cantools if that command signal spells its choices differently, or
    has none. The underlying number is unambiguous, so seed with that and leave
    the named value for display.
    """
    if hasattr(value, "value") and not isinstance(value, (int, float, bool)):
        return value.value
    return value


def _seed_group(grp, state: BoardState, overrides: dict) -> dict:
    """Full signal dict for one command message.

    A CAN frame carries every signal in its message. Sending only the edited
    ones would encode the rest as zero, so unedited signals are seeded from
    what the board reports. A signal with no readback at all and no override
    cannot be seeded, and that is an error rather than a silent zero.
    """
    values = {}
    for p in grp.params:
        if p.signal in overrides:
            values[p.signal] = overrides[p.signal]
            continue
        current = state.get(p.signal)
        seeded = current.readback() if current else None
        if seeded is None:
            raise ConfigError(
                f"cannot write {grp.message}: {p.signal} was not supplied and "
                f"the board broadcasts no readback to seed it from. Supply it "
                f"explicitly - sending the frame would encode it as zero.")
        values[p.signal] = _numeric(seeded)
    return values


def write_group(bus, db, board, message: str, values: dict,
                dry_run: bool = False) -> tuple[int, bytes]:
    """Encode and send one command message. Returns the frame that was sent."""
    arb_id, data = canbus.encode_command(db, message, values)
    if not dry_run:
        canbus.send_frame(bus, arb_id, data, extended=board.extended)
    return arb_id, data


@dataclass
class ParamResult:
    signal: str
    wanted: Any
    live: Any
    stored: Any
    live_status: str
    stored_status: str

    @property
    def ok(self) -> bool:
        """A write counts as landed when the surface that can move, moved.

        A parameter whose only readback is the EEPROM echo cannot be confirmed
        until a save, so ``stored`` differing is not yet a failure; it is
        reported and left to the caller, which is why persisting is a separate
        verb the operator invokes deliberately.
        """
        if self.live_status == MATCH:
            return True
        if self.live_status == NO_ECHO and self.stored_status == MATCH:
            return True
        return self.live_status == NO_ECHO and self.stored_status == NO_ECHO


def _compare(p, wanted, value: ParamValue | None) -> ParamResult:
    def status(echo, seen, got):
        if echo is None:
            return NO_ECHO
        if not seen:
            return UNKNOWN
        return MATCH if _equal(got, wanted) else DIFFER

    live = value.live if value else None
    stored = value.stored if value else None
    return ParamResult(
        signal=p.signal,
        wanted=wanted,
        live=live,
        stored=stored,
        live_status=status(p.live, value.live_seen if value else False, live),
        stored_status=status(p.stored, value.stored_seen if value else False,
                             stored),
    )


def _equal(got, wanted) -> bool:
    """Compare a decoded value against a written one.

    cantools hands back a NamedSignalValue for enumerated signals and a float
    for anything scaled, so an ``==`` between "AUTO" and 2, or between 70 and
    70.0, has to work.
    """
    if got is None:
        return False
    for a, b in ((got, wanted), (wanted, got)):
        if hasattr(a, "value") and not isinstance(a, (int, float)):
            a = a.value
        try:
            if float(a) == float(b):
                return True
        except (TypeError, ValueError):
            pass
    return str(got) == str(wanted)


def write_and_verify(bus, db, board, message: str, overrides: dict, *,
                     state: BoardState | None = None,
                     timeout: float = DEFAULT_READ_TIMEOUT,
                     dry_run: bool = False) -> tuple[list[ParamResult], BoardState]:
    """Write one command message, then read the board back and compare."""
    grp = params.group(board.id, message)
    known = {p.signal for p in grp.params}
    unknown = set(overrides) - known
    if unknown:
        raise ConfigError(
            f"{message} has no signal(s) {sorted(unknown)}; it carries "
            f"{sorted(known)}")

    if state is None:
        state = collect(bus, db, board.id, timeout=timeout)
    values = _seed_group(grp, state, overrides)
    write_group(bus, db, board, message, values, dry_run=dry_run)
    if dry_run:
        return [], state

    after = collect(bus, db, board.id, timeout=timeout)
    results = [_compare(p, values[p.signal], after.get(p.signal))
               for p in grp.params]
    return results, after


# ---------------------------------------------------------------------------
# Persisting
# ---------------------------------------------------------------------------


def persist(bus, db, board, dry_run: bool = False) -> tuple[int, bytes]:
    """Snapshot the board's live configuration into its EEPROM."""
    ops = params.for_board(board.id).persist
    if ops is None:
        raise ConfigError(
            f"{board.id} has no EEPROM command: its configuration cannot be "
            f"made persistent from here")
    return write_group(bus, db, board, ops.message, ops.save, dry_run=dry_run)


def load_defaults(bus, db, board, dry_run: bool = False) -> tuple[int, bytes]:
    """Restore factory defaults.

    The encoding differs per board and the differences are hostile: 0 means
    "load defaults" on KincoDrive and "no-op" on PowerStage. The value comes
    from the board's own PersistOps, never from a shared constant.
    """
    ops = params.for_board(board.id).persist
    if ops is None or ops.load_defaults is None:
        raise ConfigError(f"{board.id} has no load-defaults command")
    return write_group(bus, db, board, ops.message, ops.load_defaults,
                       dry_run=dry_run)


# ---------------------------------------------------------------------------
# Profiles
# ---------------------------------------------------------------------------


PROFILE_SCHEMA = 1


def profile_path(root: Path | str, board_id: str) -> Path:
    return Path(root) / f"{board_id}.json"


def load_profile(path: Path | str) -> dict:
    path = Path(path)
    if not path.is_file():
        raise ConfigError(f"no profile at {path}")
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise ConfigError(f"{path} is not valid JSON: {e}") from None
    if raw.get("schema") != PROFILE_SCHEMA:
        raise ConfigError(
            f"{path} has schema {raw.get('schema')!r}, expected {PROFILE_SCHEMA}")
    values = raw.get("values")
    if not isinstance(values, dict):
        raise ConfigError(f"{path} has no 'values' object")
    return raw


def save_profile(path: Path | str, board_id: str, values: dict,
                 note: str | None = None) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    body = {"schema": PROFILE_SCHEMA, "board": board_id, "values": values}
    if note:
        body["note"] = note
    path.write_text(json.dumps(body, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
    return path


def validate_profile(board_id: str, raw: dict) -> list[str]:
    """Names in the profile that this board has no such parameter for."""
    known = set(params.signals(board_id))
    return sorted(set(raw.get("values", {})) - known)


@dataclass
class Difference:
    signal: str
    wanted: Any
    current: Any
    seen: bool

    @property
    def changed(self) -> bool:
        return not (self.seen and _equal(self.current, self.wanted))


def diff(board_id: str, raw: dict, state: BoardState) -> list[Difference]:
    """Profile against the board, in the profile's declared order."""
    out = []
    for signal, wanted in raw.get("values", {}).items():
        value = state.get(signal)
        current = value.readback() if value else None
        seen = bool(value and (value.live_seen or value.stored_seen))
        out.append(Difference(signal, wanted, current, seen))
    return out


def group_overrides(board_id: str, values: dict) -> dict[str, dict]:
    """Bucket flat ``{signal: value}`` into ``{message: {signal: value}}``."""
    out: dict[str, dict] = {}
    for signal, wanted in values.items():
        grp, _ = params.find(board_id, signal)
        out.setdefault(grp.message, {})[signal] = wanted
    return out


@dataclass
class ApplyReport:
    board_id: str
    written: list[str] = field(default_factory=list)
    results: list[ParamResult] = field(default_factory=list)
    skipped: list[str] = field(default_factory=list)
    persisted: bool = False

    @property
    def ok(self) -> bool:
        return all(r.ok for r in self.results)


def apply_profile(bus, db, board, raw: dict, *,
                  changed_only: bool = True,
                  allow_actuate: bool = False,
                  timeout: float = DEFAULT_READ_TIMEOUT,
                  dry_run: bool = False) -> ApplyReport:
    """Write every parameter in a profile, verifying each message as it goes.

    ``changed_only`` skips parameters the board already agrees with, so
    re-applying a profile to a configured rig is a no-op rather than a burst of
    frames - which matters on a bus shared with live drives.
    """
    unknown = validate_profile(board.id, raw)
    if unknown:
        raise ConfigError(
            f"profile names parameter(s) {unknown} that {board.id} does not have")

    report = ApplyReport(board.id)
    state = collect(bus, db, board.id, timeout=timeout)

    wanted = dict(raw.get("values", {}))
    if changed_only:
        wanted = {d.signal: d.wanted for d in diff(board.id, raw, state)
                  if d.changed}
        report.skipped = sorted(set(raw.get("values", {})) - set(wanted))

    for message, overrides in group_overrides(board.id, wanted).items():
        grp = params.group(board.id, message)
        blocked = [p for p in grp.params
                   if p.signal in overrides and p.actuates and not allow_actuate]
        if blocked:
            report.skipped.extend(p.signal for p in blocked)
            continue
        results, state = write_and_verify(bus, db, board, message, overrides,
                                          state=state, timeout=timeout,
                                          dry_run=dry_run)
        report.written.append(message)
        report.results.extend(r for r in results if r.signal in overrides)

    report.skipped = sorted(set(report.skipped))
    return report
