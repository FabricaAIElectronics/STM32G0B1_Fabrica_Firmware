"""Discover selectable firmware folders and load whichever one is chosen.

The bench tool should not be limited to the one directory the V&V gate staged
into. In practice a folder arrives from somewhere - a release drop, a colleague,
an older build kept for comparison - and the operator needs to pick it.

A firmware folder is any directory holding either:

  * manifest.json           a staged, gate-verified set. Richest: it carries
                            CAN ids, load addresses and checksums.
  * loose .srec files       an unverified drop. Board and image kind are
                            inferred from the file names, CAN ids come from a
                            config file if present, else from the built-in
                            defaults.

Ordering is newest-first by modification time, then by name, because the folder
you want is nearly always the one you just built.

A loose folder has no checksums, so nothing can be verified against a source of
truth. That is surfaced, not hidden: `trusted` is False and the UI says so.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import manifest as mf

#: Recognised in file names, longest first so "kincodrive" wins over "kinco".
_BOARD_TOKENS = (
    ("kincodrive", ("kincodrive", "kinco", "actuation_io", "actuation")),
    ("powerstage", ("powerstage", "power_stage")),
    ("leddriver", ("leddriver", "led_driver", "ledd")),
    ("knob", ("knob", "stm32f3re", "f3re", "stm32f3_prog", "f3_prog", "f303")),
)
_BOOT_TOKENS = ("boot", "bootloader", "blt")

#: Fallback CAN ids and load addresses when a loose folder has no config.
#: Same values as vv/boards.py; duplicated deliberately so the bench tool has no
#: dependency on the firmware repo's layout.
_DEFAULTS = {
    "kincodrive": {"blt_rx": 0x101, "blt_tx": 0x102, "mcu": "STM32G0B1RET6",
                   "app_origin": "0x08003000", "dbc": "KincoDrive_ControlModule.dbc"},
    "powerstage": {"blt_rx": 0x130, "blt_tx": 0x131, "mcu": "STM32G0B1RET6",
                   "app_origin": "0x08003000", "dbc": "PowerStage.dbc"},
    "leddriver": {"blt_rx": 0x160, "blt_tx": 0x161, "mcu": "STM32G0B1RET6",
                  "app_origin": "0x08003000", "dbc": "LEDDriver.dbc"},
    "knob": {"blt_rx": 0x667, "blt_tx": 0x7E1, "mcu": "STM32F303RET6",
             "app_origin": "0x08003800", "dbc": "Knob.dbc"},
}

CONFIG_NAMES = ("fabrica.json", "config.json", "boards.json")


@dataclass
class FirmwareSource:
    path: Path
    mtime: float
    kind: str                     # "manifest" or "loose"
    srec_count: int
    dbc_count: int
    config: Path | None = None
    boards: tuple[str, ...] = ()
    git_sha: str = ""
    git_dirty: bool = False
    notes: list[str] = field(default_factory=list)

    @property
    def trusted(self) -> bool:
        """A manifest carries checksums; a loose folder cannot be verified."""
        return self.kind == "manifest"

    @property
    def label(self) -> str:
        who = ", ".join(self.boards) if self.boards else "?"
        prov = (f"git {self.git_sha[:8]}{' DIRTY' if self.git_dirty else ''}"
                if self.git_sha else "unverified")
        return f"{self.path.name}  [{who}]  {prov}"


def board_from_filename(name: str) -> tuple[str | None, str]:
    """Infer (board id, "boot"|"app") from a file name."""
    low = name.lower()
    board = None
    for bid, tokens in _BOARD_TOKENS:
        if any(t in low for t in tokens):
            board = bid
            break
    kind = "boot" if any(t in low for t in _BOOT_TOKENS) else "app"
    return board, kind


def _mtime(path: Path) -> float:
    """Newest interesting file in the folder, else the folder itself."""
    best = path.stat().st_mtime
    for pattern in ("*.srec", "manifest.json", "*.dbc"):
        for f in path.glob(pattern):
            best = max(best, f.stat().st_mtime)
    return best


def _is_candidate(path: Path) -> bool:
    """Is this folder itself a firmware set?

    Three shapes count, and all three turn up in practice:

      my-build/manifest.json          staged by the gate
      my-build/*.srec                 images dropped straight into a folder
      my-build/<board>/*.srec         images dropped in per-board subfolders

    The third matters because operators copy versions in by hand: without it, a
    folder holding four board subdirectories and no manifest was reported as
    four separate firmware sets rather than one, and the picker listed
    'kincodrive', 'knob', 'leddriver', 'powerstage' where the version name
    should have been.

    The third case is why the subfolders must be named after BOARDS rather than
    just "contains a .srec somewhere one level down". firmware/ holds version
    folders which hold board folders, so a plain one-level-down test makes
    firmware/ itself look like a set - discover() then returns the search root
    and the versions inside it disappear, which is the exact failure the
    original non-recursive rule existed to prevent.

    Still deliberately NOT recursive: the subdirectory check looks exactly one
    level down, and only at names that are known board ids.
    """
    if (path / "manifest.json").is_file() or any(path.glob("*.srec")):
        return True
    try:
        children = [c for c in path.iterdir()
                    if c.is_dir() and c.name.lower() in _DEFAULTS]
    except OSError:
        return False
    return any(any(c.glob("*.srec")) for c in children)


def _inspect(path: Path) -> FirmwareSource | None:
    manifest = path / "manifest.json"
    if not _is_candidate(path):
        return None
    # Counting IS recursive: a staged folder keeps its images in <board>/ subdirs.
    srecs = sorted(path.rglob("*.srec"))

    config = next((path / n for n in CONFIG_NAMES if (path / n).is_file()), None)
    src = FirmwareSource(
        path=path, mtime=_mtime(path),
        kind="manifest" if manifest.is_file() else "loose",
        srec_count=len(srecs), dbc_count=len(list(path.rglob("*.dbc"))),
        config=config,
    )

    if src.kind == "manifest":
        try:
            raw = json.loads(manifest.read_text(encoding="utf-8"))
            src.boards = tuple(b["id"] for b in raw.get("boards", []))
            src.git_sha = raw.get("git_sha", "")
            src.git_dirty = bool(raw.get("git_dirty", False))
            if src.git_dirty:
                src.notes.append("staged from a dirty tree")
        except (json.JSONDecodeError, KeyError, TypeError) as exc:
            src.notes.append(f"manifest unreadable: {exc}")
            src.kind = "loose"
    if src.kind == "loose":
        found = {board_from_filename(f.name)[0] for f in srecs}
        src.boards = tuple(sorted(b for b in found if b))
        unknown = sum(1 for f in srecs if board_from_filename(f.name)[0] is None)
        if unknown:
            src.notes.append(f"{unknown} .srec not attributable to a board")
        src.notes.append("no manifest: images cannot be checksum-verified")
    return src


def discover(root: Path | str, max_depth: int = 2) -> list[FirmwareSource]:
    """Find firmware folders under root, newest first then by name.

    The root itself counts as a candidate, so pointing at a single folder works
    as well as pointing at a directory of them.
    """
    root = Path(root)
    if not root.is_dir():
        return []

    candidates: list[Path] = []

    def visit(d: Path, depth: int) -> None:
        if depth > max_depth:
            return
        if _is_candidate(d):
            candidates.append(d)
            return          # a set's own subdirectories are part of it
        try:
            children = sorted(d.iterdir())
        except OSError:
            return
        for child in children:
            if child.is_dir() and not child.name.startswith("."):
                visit(child, depth + 1)

    visit(root, 0)
    found = [s for s in (_inspect(d) for d in candidates) if s]
    # Newest first; name as the tie-break so the order is stable.
    found.sort(key=lambda s: (-s.mtime, s.path.name.lower()))
    return found


def _synthesise_manifest(src: FirmwareSource) -> dict:
    """Build a manifest-shaped dict for a loose folder."""
    overrides = {}
    if src.config:
        try:
            raw = json.loads(src.config.read_text(encoding="utf-8"))
            overrides = {b["id"]: b for b in raw.get("boards", [])} \
                if "boards" in raw else raw
        except (json.JSONDecodeError, TypeError, KeyError):
            src.notes.append(f"config {src.config.name} unreadable; ignoring")

    by_board: dict[str, dict] = {}
    for f in sorted(src.path.rglob("*.srec")):
        bid, kind = board_from_filename(f.name)
        if bid is None:
            continue
        d = _DEFAULTS[bid] | overrides.get(bid, {})
        entry = by_board.setdefault(bid, {
            "id": bid, "name": bid.title(), "mcu": d["mcu"],
            "can": {"blt_rx": f"0x{d['blt_rx']:03X}" if isinstance(d["blt_rx"], int)
                    else d["blt_rx"],
                    "blt_tx": f"0x{d['blt_tx']:03X}" if isinstance(d["blt_tx"], int)
                    else d["blt_tx"],
                    "bitrate": d.get("bitrate", 500000), "extended": False},
            "dbc": d.get("dbc"), "address_plan_exempt": bid == "knob"})
        entry[kind] = {
            "file": str(f.relative_to(src.path)).replace("\\", "/"),
            "sha256": mf.sha256_of(f),          # computed now, so it is self-consistent
            "flash_bytes": srec_payload_bytes(f),
            "load_addr": "0x08000000" if kind == "boot" else d["app_origin"],
        }

    complete = [b for b in by_board.values() if "boot" in b and "app" in b]
    for b in by_board.values():
        if b not in complete:
            missing = "app" if "boot" in b else "boot"
            src.notes.append(f"{b['id']}: no {missing} image, board omitted")
    return {"schema": mf.SCHEMA, "generated": "", "git_sha": "", "git_dirty": True,
            "gate": "unverified", "boards": complete}


#: Address field width in bytes for each S-record data type.
_SREC_ADDR_BYTES = {"1": 2, "2": 3, "3": 4}


def srec_payload_bytes(path: Path) -> int:
    """Bytes an S-record actually programs into flash.

    NOT the size of the file. S-records are ASCII hex with a type, byte count,
    address and checksum per line, so the file is about 3x the payload - the
    PowerStage application is a 163,210-byte file carrying 54,368 bytes of
    image. Reporting the file size made the TUI show `app 163210 B` against a
    512 KB part, which is wrong by enough to matter when judging headroom.

    Each data line is S<type><count><address><data><checksum>, where count
    covers everything after itself, so the data length is
    count - address width - 1 for the checksum. Non-data records (S0 header,
    S5/S6 counts, S7/S8/S9 start address) carry no image bytes and are skipped.

    Slightly smaller than the manifest's flash_bytes for the same image (8,916
    vs 10,180 for the G0B1 bootloader): the gate uses `size`'s text+data from
    the ELF, which is the figure the reserved-region limit is written against.
    A loose folder has no ELF, so the payload is the closest honest answer -
    do not "reconcile" the two by going back to the file size.
    """
    total = 0
    try:
        text = path.read_text(encoding="ascii", errors="replace")
    except OSError:
        return 0
    for line in text.splitlines():
        line = line.strip()
        if len(line) < 4 or line[0] not in "sS":
            continue
        addr_bytes = _SREC_ADDR_BYTES.get(line[1])
        if addr_bytes is None:          # S0/S5/S6/S7/S8/S9 carry no image data
            continue
        try:
            count = int(line[2:4], 16)
        except ValueError:
            continue
        data_len = count - addr_bytes - 1
        if data_len > 0:
            total += data_len
    return total


def load(src: FirmwareSource) -> mf.Manifest:
    """Load a source as a Manifest, synthesising one for a loose folder.

    A synthesised manifest reports gate="unverified" and git_dirty=True, so
    everything downstream - doctor, the TUI status bar - already surfaces it as
    firmware of unknown provenance without needing to know it was synthesised.
    """
    if src.kind == "manifest":
        return mf.load_manifest(src.path)

    raw = _synthesise_manifest(src)
    if not raw["boards"]:
        raise mf.ManifestError(
            f"{src.path}: no complete board found. Expected .srec names "
            f"identifying a board and whether it is the bootloader, e.g. "
            f"'powerstage_boot.srec' and 'powerstage_app.srec'.")
    return mf.manifest_from_dict(raw, src.path,
                                 describe=f"{src.path} (synthesised)")
