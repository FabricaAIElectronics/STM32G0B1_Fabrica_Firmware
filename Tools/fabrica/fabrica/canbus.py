"""Live CAN traffic: open a bus, decode frames against a DBC, send commands.

Where :mod:`fabrica.canflash` *writes firmware* over CAN (by shelling out to
BootCommander), this module is the *runtime* half of the tool: watching what a
board says while it runs, and pushing configuration at it. The TUI sits on top
of :class:`Monitor`.

Written without hardware access, so the entire module is exercised through
python-can's ``virtual`` backend: two buses opened on the same channel name see
each other's traffic in-process, with no SocketCAN, no driver and no wire. The
only thing that changes on the bench is the ``interface`` argument to
:func:`open_bus` -- everything downstream of it is identical.

Two rules this module lives by
------------------------------
1. **Decoding never raises.** :func:`decode_frame` returns a
   :class:`DecodedFrame` for *every* input: an id that is not in the DBC, a
   truncated payload, a board sending garbage, or no DBC at all. A bench tool
   that dies on one malformed frame is useless precisely when you need it --
   during a bringup where the malformed frame *is* the bug you are hunting.
2. **A board may have no DBC.** ``manifest.BoardImages.dbc`` is a file *name*
   or ``None`` (the knob board has no DBC at all). ``db=None`` is a supported,
   fully working mode everywhere here: you still see ids, payloads, counts and
   rates, just no signal names.

Signal values are decoded with ``decode_choices=False``, i.e. ``Fan_Mode`` comes
back as ``2``, not ``NamedSignalValue('AUTO')``. Plain ints and floats are
JSON-serialisable, compare predictably, and log cleanly; a UI that wants the
enum label can look it up in the DBC it already holds.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Deque

import can
import cantools

from .canflash import CAN_EXT_ID_MAX, CAN_STD_ID_MAX, build_reset_frame

#: Return raw numeric signal values rather than cantools' NamedSignalValue.
#: See the module docstring for the reasoning.
DECODE_CHOICES = False

#: How many arrival timestamps :class:`Monitor` keeps per id for rate estimation.
#: Enough to average out jitter on a 10 Hz broadcast without unbounded growth.
RATE_WINDOW = 32


class DbcError(RuntimeError):
    """The DBC file is missing or cantools could not parse it."""


# --------------------------------------------------------------------------
# Bus
# --------------------------------------------------------------------------


def open_bus(iface: str, interface: str = "socketcan", **kw) -> can.BusABC:
    """Open a CAN bus. Thin wrapper over ``can.Bus`` -- one place to patch.

    ``iface`` is the python-can *channel*: ``"can0"`` for socketcan on the
    Jetson/RPi, or any shared string for ``interface="virtual"``, which is what
    the tests use so that nothing touches hardware.

    Note that ``socketcan`` is resolved lazily by python-can, so importing this
    module on Windows (where socketcan does not exist) is safe.
    """
    if not iface:
        raise ValueError("a CAN channel name is required (e.g. 'can0')")
    return can.Bus(channel=iface, interface=interface, **kw)


# --------------------------------------------------------------------------
# DBC
# --------------------------------------------------------------------------


def load_dbc(path: Path | str):
    """Load a DBC and return the cantools database.

    Raises :class:`DbcError` -- not a bare cantools traceback -- naming the path
    that was actually looked at, because the usual cause is a manifest ``dbc``
    field being resolved against the wrong directory.
    """
    p = Path(path)
    if not p.is_file():
        raise DbcError(
            f"no DBC file at {p}\n"
            "The manifest stores a bare file name (e.g. 'PowerStage.dbc'); "
            "resolve it against the board's source directory."
        )
    try:
        return cantools.database.load_file(p)
    except Exception as exc:  # cantools raises several unrelated types
        raise DbcError(f"could not parse {p}: {exc}") from exc


#: Tools/fabrica/fabrica/canbus.py -> the repository root, where the per-board
#: .dbc files live alongside their firmware projects.
DEFAULT_DBC_SEARCH_ROOT = Path(__file__).resolve().parents[3]


def find_dbc(name: str | None,
             search_root: Path | str | None = None) -> Path | None:
    """Resolve a manifest ``dbc`` file name to a path under ``search_root``.

    ``search_root`` defaults to the repository root. It used to be required,
    which meant both real callers -- the monitor command and the TUI -- raised
    TypeError the moment a board actually had a DBC. Every test passed because
    every test supplied the argument; only running the CLI caught it.

    ``None`` in, ``None`` out: a board with no DBC (the knob board) is a normal
    case, not an error. ``None`` is also returned when the named file simply is
    not there, so a caller can fall back to raw-id mode instead of aborting.
    """
    if not name:
        return None
    root = Path(search_root) if search_root is not None else DEFAULT_DBC_SEARCH_ROOT
    if not root.is_dir():
        return None
    direct = root / name
    if direct.is_file():
        return direct
    for found in sorted(root.rglob(name)):
        if found.is_file():
            return found
    return None


# --------------------------------------------------------------------------
# Decoding
# --------------------------------------------------------------------------


@dataclass
class DecodedFrame:
    """One observed frame, decoded as far as it could be.

    ``name`` is ``None`` when the id is not in the DBC (or there is no DBC), and
    ``signals`` is ``{}`` when the payload could not be decoded. ``raw`` always
    holds exactly the bytes that arrived, decoded or not -- it is the only field
    guaranteed to be trustworthy for an unrecognised frame.
    """

    arb_id: int
    name: str | None
    signals: dict
    raw: bytes
    timestamp: float

    @property
    def known(self) -> bool:
        """True when the id was found in the DBC."""
        return self.name is not None

    @property
    def decoded(self) -> bool:
        """True when at least one signal came out."""
        return bool(self.signals)


def decode_frame(db: Any, arb_id: int, data: bytes, timestamp: float = 0.0) -> DecodedFrame:
    """Decode one frame. **Never raises.**

    Degrades in three steps rather than failing:

    * no DBC, or an id the DBC does not define -> ``name=None, signals={}``
    * id known but the payload is short/long/nonsense -> ``name`` set,
      ``signals={}``
    * everything fine -> both populated

    ``raw`` is preserved in all three cases, so a frame this function could not
    understand is still fully visible to the operator.
    """
    raw = bytes(data) if data is not None else b""

    if db is None:
        return DecodedFrame(arb_id, None, {}, raw, timestamp)

    # Step 1: is the id even in the database?
    try:
        message = db.get_message_by_frame_id(arb_id)
        name = message.name
    except Exception:
        # KeyError for an unknown id; anything else means a database we cannot
        # interrogate, which is equally "we do not know this frame".
        return DecodedFrame(arb_id, None, {}, raw, timestamp)

    # Step 2: does the payload decode? A wrong DLC is the common real failure
    # (a board mid-firmware-change), and it must not take the monitor down.
    try:
        signals = dict(db.decode_message(arb_id, raw, decode_choices=DECODE_CHOICES))
    except Exception:
        signals = {}

    return DecodedFrame(arb_id, name, signals, raw, timestamp)


# --------------------------------------------------------------------------
# Monitor
# --------------------------------------------------------------------------


@dataclass
class _Track:
    """Per-id bookkeeping."""

    latest: DecodedFrame
    count: int = 0
    stamps: Deque[float] = field(default_factory=lambda: deque(maxlen=RATE_WINDOW))


class Monitor:
    """In-memory roll-up of observed traffic: latest frame, count and rate per id.

    Deliberately passive and thread-free. It owns no bus and starts no reader;
    the caller pumps frames in with :meth:`observe`. That keeps it trivially
    testable (feed it synthetic timestamps) and leaves the choice of
    ``bus.recv`` loop, ``can.Notifier`` or replay entirely to the TUI.
    """

    def __init__(self, db: Any = None):
        self.db = db
        self._tracks: dict[int, _Track] = {}

    def observe(self, arb_id: int, data: bytes, timestamp: float) -> DecodedFrame:
        """Decode and record one frame. Returns the decoded result."""
        frame = decode_frame(self.db, arb_id, data, timestamp)
        track = self._tracks.get(arb_id)
        if track is None:
            track = _Track(latest=frame)
            self._tracks[arb_id] = track
        else:
            track.latest = frame
        track.count += 1
        track.stamps.append(timestamp)
        return frame

    def snapshot(self) -> list[DecodedFrame]:
        """Latest frame per id, sorted by arbitration id.

        Sorted so a rendered table does not reshuffle rows as traffic arrives --
        a jumping list is unreadable on a bench.
        """
        return [self._tracks[i].latest for i in sorted(self._tracks)]

    def counts(self) -> dict[int, int]:
        """Frames seen per id, ordered by id."""
        return {i: self._tracks[i].count for i in sorted(self._tracks)}

    def rate_hz(self, arb_id: int) -> float | None:
        """Estimated frame rate for ``arb_id``, or ``None`` if not measurable.

        Averaged over the whole retained window rather than the last interval,
        so one late frame does not make the number jump. ``None`` means "not
        enough information yet": an unseen id, a single observation, or
        timestamps that did not advance.
        """
        track = self._tracks.get(arb_id)
        if track is None or len(track.stamps) < 2:
            return None
        span = track.stamps[-1] - track.stamps[0]
        if span <= 0:
            return None
        return (len(track.stamps) - 1) / span

    def clear(self) -> None:
        """Forget everything observed so far. The DBC is kept."""
        self._tracks.clear()

    @property
    def ids(self) -> list[int]:
        """Arbitration ids seen so far, sorted."""
        return sorted(self._tracks)

    def __len__(self) -> int:
        return len(self._tracks)


# --------------------------------------------------------------------------
# Sending
# --------------------------------------------------------------------------


def send_frame(bus, arb_id: int, data: bytes, extended: bool = False) -> None:
    """Put one frame on the bus.

    Range-checks the id against the same limits :mod:`fabrica.canflash` uses, so
    a typo'd id is rejected here instead of surfacing as an opaque driver error.
    """
    if not isinstance(arb_id, int) or isinstance(arb_id, bool):
        raise TypeError(f"arb_id must be an int, got {arb_id!r}")
    limit = CAN_EXT_ID_MAX if extended else CAN_STD_ID_MAX
    if not 0 <= arb_id <= limit:
        kind = "29-bit extended" if extended else "11-bit standard"
        raise ValueError(
            f"CAN id 0x{arb_id:X} is out of range for a {kind} id (0x0..0x{limit:X})"
        )
    payload = bytes(data)
    if len(payload) > 8:
        raise ValueError(f"payload is {len(payload)} bytes; classic CAN allows at most 8")
    bus.send(can.Message(arbitration_id=arb_id, data=payload, is_extended_id=extended))


def send_reset(bus, blt_rx: int) -> None:
    """Send the "enter bootloader" trigger to a running application.

    The frame itself is built by :func:`fabrica.canflash.build_reset_frame`, so
    the reset payload is defined in exactly one place -- this is the same frame
    the flash path sends before handing over to BootCommander.
    """
    arb_id, payload = build_reset_frame(blt_rx)
    send_frame(bus, arb_id, payload, extended=arb_id > CAN_STD_ID_MAX)


def encode_command(db: Any, message_name: str, signals: dict) -> tuple[int, bytes]:
    """Encode a command by DBC message name. Returns ``(arbitration_id, data)``.

    This is how configuration is written: the caller names signals
    (``{"Fan_Mode": 2, "Fan_Duty": 75}``) and the DBC decides bit positions,
    byte order and scaling. Nothing in the tool hand-packs a config frame.

    Raises ``KeyError`` listing the available message names when
    ``message_name`` is unknown -- on a bench you want the menu, not a
    stack trace. Also raises when ``db`` is ``None``: a board with no DBC has no
    message names to encode against, and silently sending nothing would be worse.
    """
    if db is None:
        raise ValueError(
            f"cannot encode {message_name!r}: this board has no DBC "
            "(manifest dbc is null). Send a raw frame with send_frame() instead."
        )
    try:
        message = db.get_message_by_name(message_name)
    except KeyError:
        available = ", ".join(sorted(m.name for m in db.messages)) or "(none)"
        raise KeyError(
            f"unknown message {message_name!r}; DBC has: {available}"
        ) from None
    return message.frame_id, bytes(message.encode(signals))


def message_names(db: Any) -> list[str]:
    """Sorted message names in ``db``; empty list when there is no DBC."""
    if db is None:
        return []
    return sorted(m.name for m in db.messages)
