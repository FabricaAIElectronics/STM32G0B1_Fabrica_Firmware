"""Sub-project C - stage verified .srec artifacts plus a manifest for deployment.

Runs only after a fully green gate, so an unverified binary can never reach
Tools/. The manifest binds each image to the board it belongs to; before it
existed, all three G0B1 bootloaders emitted an identically named .srec differing
only in the CAN address they answer on.
"""
import hashlib
import json
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from vv.boards import BOARDS, REPO_ROOT
from vv.checks.build import build_project

FIRMWARE_DIR = REPO_ROOT / "Tools" / "fabrica" / "firmware"
MANIFEST_PATH = FIRMWARE_DIR / "manifest.json"


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def git_info() -> tuple[str, bool]:
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT,
                         capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=REPO_ROOT,
                                capture_output=True, text=True).stdout.strip())
    return sha, dirty


def _copy_srec(board, project_dir: str, eclipse_name: str, load_addr: int,
               kind: str) -> dict:
    src = REPO_ROOT / project_dir / "Debug" / f"{eclipse_name}.srec"
    if not src.is_file():
        raise FileNotFoundError(f"{kind} artifact missing: {src}")
    dest_dir = FIRMWARE_DIR / board.id
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / src.name
    shutil.copy2(src, dest)
    built = build_project(project_dir, eclipse_name)
    return {
        "file": f"{board.id}/{src.name}",
        "sha256": sha256_of(dest),
        "flash_bytes": built["text"] + built["data"],
        "load_addr": f"0x{load_addr:08X}",
    }


def collect_board_artifacts(board) -> dict:
    return {
        "boot": _copy_srec(board, board.boot_dir, board.boot_eclipse, 0x08000000, "boot"),
        "app": _copy_srec(board, board.app_dir, board.app_eclipse, board.app_origin, "app"),
    }


def build_manifest(board_entries: list[dict], gate_passed: bool) -> dict:
    sha, dirty = git_info()
    return {
        "schema": 1,
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "git_sha": sha,
        "git_dirty": dirty,
        "gate": "pass" if gate_passed else "fail",
        "boards": board_entries,
    }


def stage_artifacts(gate_passed: bool) -> dict:
    if not gate_passed:
        raise RuntimeError("gate did not pass; refusing to stage artifacts")

    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    entries = []
    for board in BOARDS:
        art = collect_board_artifacts(board)
        entries.append({
            "id": board.id,
            "name": board.name,
            "mcu": board.mcu,
            "boot": art["boot"],
            "app": art["app"],
            "can": {
                "blt_rx": f"0x{board.blt_rx:03X}",
                "blt_tx": f"0x{board.blt_tx:03X}",
                "bitrate": board.bitrate,
                "extended": board.extended,
            },
            "dbc": Path(board.dbc).name if board.dbc else None,
            "address_plan_exempt": board.address_plan_exempt,
        })

    manifest = build_manifest(entries, gate_passed)
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest
