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

#: Container of named firmware versions, NOT a firmware set itself. Each
#: staged build lands in its own subfolder, and operators copy their own
#: versions in beside them - the TUI's `f` picker lists whatever is here,
#: newest first, by whatever name the folder has.
FIRMWARE_ROOT = REPO_ROOT / "Tools" / "fabrica" / "firmware"

#: The version folder being written is NOT module state. It was, and
#: stage_artifacts() reassigned it per run, which meant a caller could not
#: redirect staging without the reassignment clobbering the redirection - the
#: vv test suite monkeypatched it and had its artifacts written into the real
#: Tools/fabrica/firmware/ tree anyway. The destination is now a parameter
#: threaded from stage_artifacts() down to each copy helper.


def default_version_label() -> str:
    """Date plus short sha, e.g. 2026-08-03-31cbbc2.

    Only a default: the operator names their own folders, since they are the
    ones copying them onto a bench. This just has to be unique, sort sensibly
    by name as well as by mtime, and stay traceable to a commit.
    """
    sha, dirty = git_info()
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    label = f"{stamp}-{sha[:7]}" if sha else stamp
    return label + ("-dirty" if dirty else "")


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
               kind: str, dest_root: Path) -> dict:
    """Build, then copy, then hash - in that order.

    The build must come first. Copying first and building afterwards let the
    build regenerate Debug/<name>.srec, so the staged bytes and their sha256
    described the PREVIOUS build while flash_bytes in the same entry described
    the new one. The hash stayed self-consistent with the staged file, which is
    exactly what makes that kind of skew hard to notice later.

    flash_bytes is `size`'s text+data rather than the S-record payload. The two
    genuinely differ (8916 vs 10180 bytes for the G0B1 bootloader), and this
    must agree with the size stage, which gates the reserved-region limit.
    """
    built = build_project(project_dir, eclipse_name)
    if not built["ok"]:
        raise RuntimeError(
            f"{kind} build failed for {eclipse_name}: {'; '.join(built['errors'][:3])}")

    src = REPO_ROOT / project_dir / "Debug" / f"{eclipse_name}.srec"
    if not src.is_file():
        raise FileNotFoundError(f"{kind} artifact missing after build: {src}")

    dest_dir = dest_root / board.id
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / src.name
    shutil.copy2(src, dest)

    return {
        "file": f"{board.id}/{src.name}",
        "sha256": sha256_of(dest),
        "flash_bytes": built["text"] + built["data"],
        "load_addr": f"0x{load_addr:08X}",
    }


def _copy_dbc(board, dest_root: Path) -> None:
    """Stage the board's DBC beside its images.

    Without this the staged tree is not self-contained: the DBCs live in the
    firmware project directories, and canbus.find_dbc falls back to the repo
    root, so copying Tools/fabrica to a bench on its own produced

        no DBC for knob; cannot tell which broadcasts to expect

    and monitor/verify silently degraded to raw ids. The manifest already names
    the file, so shipping it with the images is what makes that name resolvable
    away from the checkout.
    """
    if not board.dbc:
        return
    src = REPO_ROOT / board.dbc
    if not src.is_file():
        return
    dest_dir = dest_root / board.id
    dest_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest_dir / src.name)


def collect_board_artifacts(board, dest_root: Path) -> dict:
    artifacts = {
        "boot": _copy_srec(board, board.boot_dir, board.boot_eclipse, 0x08000000,
                           "boot", dest_root),
        "app": _copy_srec(board, board.app_dir, board.app_eclipse, board.app_origin,
                          "app", dest_root),
    }
    _copy_dbc(board, dest_root)
    return artifacts


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


def stage_artifacts(gate_passed: bool, version: str | None = None,
                    firmware_root: Path | None = None) -> tuple[dict, Path]:
    """Stage every board into firmware/<version>/.

    `version` names the folder; it defaults to date+sha. Nothing else in the
    tool cares what it is called - discover() finds sets by shape, not by name -
    so an operator can drop `bench-test-A/` in beside it and pick either.

    `firmware_root` overrides the container. It exists so a caller - a test, or
    anything staging outside the checkout - can redirect the write and have it
    actually go there.

    Returns the manifest and the version folder it was written to.
    """
    if not gate_passed:
        raise RuntimeError("gate did not pass; refusing to stage artifacts")

    label = version or default_version_label()
    # Reject path separators rather than silently staging somewhere else.
    if "/" in label or "\\" in label or label in (".", ".."):
        raise ValueError(f"invalid version label {label!r}: it names a folder, "
                         f"not a path")
    dest_root = (firmware_root or FIRMWARE_ROOT) / label
    manifest_path = dest_root / "manifest.json"

    dest_root.mkdir(parents=True, exist_ok=True)
    entries = []
    for board in BOARDS:
        art = collect_board_artifacts(board, dest_root)
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
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest, dest_root
