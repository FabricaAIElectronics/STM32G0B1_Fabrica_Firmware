"""Read and verify the firmware manifest produced by the V&V gate.

The manifest is what makes flashing safe. Before it existed, all three G0B1
bootloaders emitted an identically named .srec differing only in the CAN address
they answer on, so flashing the wrong one produced a board that was alive but
silent on the address you expected. Every flash operation in this tool resolves
its image through here, and every image is checksum-verified before it is sent.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

SCHEMA = 1

# Tools/fabrica/fabrica/manifest.py -> Tools/fabrica/firmware/
DEFAULT_FIRMWARE_DIR = Path(__file__).resolve().parent.parent / "firmware"


class ManifestError(RuntimeError):
    """The manifest is missing, malformed, or disagrees with the files on disk."""


@dataclass(frozen=True)
class Image:
    kind: str          # "boot" or "app"
    file: str          # path relative to the firmware directory
    sha256: str
    flash_bytes: int
    load_addr: str     # "0x08000000"

    @property
    def load_addr_int(self) -> int:
        return int(self.load_addr, 16)


@dataclass(frozen=True)
class BoardImages:
    id: str
    name: str
    mcu: str
    boot: Image
    app: Image
    blt_rx: int
    blt_tx: int
    bitrate: int
    extended: bool
    dbc: str | None
    address_plan_exempt: bool

    def image(self, kind: str) -> Image:
        if kind == "boot":
            return self.boot
        if kind == "app":
            return self.app
        raise KeyError(f"unknown image kind {kind!r}; expected 'boot' or 'app'")


@dataclass(frozen=True)
class Manifest:
    git_sha: str
    git_dirty: bool
    generated: str
    gate: str
    boards: tuple[BoardImages, ...]
    root: Path

    def board(self, board_id: str) -> BoardImages:
        for b in self.boards:
            if b.id == board_id:
                return b
        known = ", ".join(b.id for b in self.boards)
        raise KeyError(f"unknown board {board_id!r}; manifest has: {known}")

    @property
    def board_ids(self) -> list[str]:
        return [b.id for b in self.boards]

    def path_of(self, image: Image) -> Path:
        return self.root / image.file


def _image(kind: str, raw: dict) -> Image:
    missing = {"file", "sha256", "flash_bytes", "load_addr"} - set(raw)
    if missing:
        raise ManifestError(f"{kind} image missing keys: {', '.join(sorted(missing))}")
    return Image(kind=kind, file=raw["file"], sha256=raw["sha256"],
                 flash_bytes=int(raw["flash_bytes"]), load_addr=raw["load_addr"])


def load_manifest(firmware_dir: Path | str | None = None) -> Manifest:
    """Parse manifest.json. Raises ManifestError with an actionable message."""
    root = Path(firmware_dir) if firmware_dir else DEFAULT_FIRMWARE_DIR
    path = root / "manifest.json"
    if not path.is_file():
        raise ManifestError(
            f"no manifest at {path}\n"
            "Generate one with:  python vv/run_gate.py --stage-artifacts")
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ManifestError(f"{path} is not valid JSON: {exc}") from exc

    return manifest_from_dict(raw, root, describe=str(path))


def manifest_from_dict(raw: dict, root: Path,
                       describe: str = "manifest") -> Manifest:
    """Build a Manifest from an already-parsed dict rooted at `root`.

    Split out so a synthesised manifest - one inferred from a folder of loose
    .srec files - goes through exactly the same validation as a staged one,
    rather than a parallel code path that could drift.
    """
    if raw.get("schema") != SCHEMA:
        raise ManifestError(
            f"{describe} has schema {raw.get('schema')!r}, "
            f"this tool expects {SCHEMA}")

    boards = []
    for entry in raw.get("boards", []):
        can = entry.get("can", {})
        boards.append(BoardImages(
            id=entry["id"],
            name=entry.get("name", entry["id"]),
            mcu=entry.get("mcu", "unknown"),
            boot=_image("boot", entry["boot"]),
            app=_image("app", entry["app"]),
            blt_rx=int(can["blt_rx"], 16),
            blt_tx=int(can["blt_tx"], 16),
            bitrate=int(can.get("bitrate", 500000)),
            extended=bool(can.get("extended", False)),
            dbc=entry.get("dbc"),
            address_plan_exempt=bool(entry.get("address_plan_exempt", False)),
        ))
    if not boards:
        raise ManifestError(f"{describe} lists no boards")

    return Manifest(
        git_sha=raw.get("git_sha", "unknown"),
        git_dirty=bool(raw.get("git_dirty", False)),
        generated=raw.get("generated", "unknown"),
        gate=raw.get("gate", "unknown"),
        boards=tuple(boards),
        root=root,
    )


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_image(manifest: Manifest, image: Image) -> None:
    """Raise ManifestError unless the file exists and its checksum matches.

    Called immediately before every flash. A corrupted or swapped image must
    never reach a board.
    """
    path = manifest.path_of(image)
    if not path.is_file():
        raise ManifestError(f"image missing: {path}")
    actual = sha256_of(path)
    if actual != image.sha256:
        raise ManifestError(
            f"checksum mismatch for {image.file}\n"
            f"  manifest: {image.sha256}\n"
            f"  on disk : {actual}\n"
            "Re-stage with:  python vv/run_gate.py --stage-artifacts")


def verify_all(manifest: Manifest) -> list[str]:
    """Verify every image. Returns a list of human-readable problems."""
    problems = []
    for board in manifest.boards:
        for kind in ("boot", "app"):
            try:
                verify_image(manifest, board.image(kind))
            except ManifestError as exc:
                problems.append(f"{board.id}/{kind}: {exc}")
    return problems
