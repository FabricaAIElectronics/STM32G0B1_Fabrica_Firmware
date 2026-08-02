"""Tests for firmware-folder discovery and loading."""
from __future__ import annotations

import hashlib
import json
import os
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fabrica import manifest as mf, sources  # noqa: E402


def make_srec(path: Path, text: str = "S0 test\n") -> bytes:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = text.encode()
    path.write_bytes(data)
    return data


def make_loose(root: Path, name: str, boards=("powerstage",), mtime=None) -> Path:
    d = root / name
    for b in boards:
        make_srec(d / f"{b}_boot.srec", f"S0 {b} boot\n")
        make_srec(d / f"{b}_app.srec", f"S0 {b} app\n")
    if mtime is not None:
        for f in list(d.rglob("*")) + [d]:
            os.utime(f, (mtime, mtime))
    return d


def make_staged(root: Path, name: str, *, git_sha="abc12345", dirty=False) -> Path:
    d = root / name
    boards = []
    for bid, rx, tx, org in (("powerstage", "0x130", "0x131", "0x08003000"),):
        entry = {"id": bid, "name": bid.title(), "mcu": "STM32G0B1RET6",
                 "can": {"blt_rx": rx, "blt_tx": tx, "bitrate": 500000,
                         "extended": False},
                 "dbc": None, "address_plan_exempt": False}
        for kind, addr in (("boot", "0x08000000"), ("app", org)):
            data = make_srec(d / bid / f"{bid}_{kind}.srec", f"S0 {bid} {kind}\n")
            entry[kind] = {"file": f"{bid}/{bid}_{kind}.srec",
                           "sha256": hashlib.sha256(data).hexdigest(),
                           "flash_bytes": len(data), "load_addr": addr}
        boards.append(entry)
    (d / "manifest.json").write_text(json.dumps(
        {"schema": 1, "generated": "2026-08-01T00:00:00Z", "git_sha": git_sha,
         "git_dirty": dirty, "gate": "pass", "boards": boards}), encoding="utf-8")
    return d


# ---------------------------------------------------------------- ordering --
def test_newest_folder_comes_first(tmp_path):
    now = time.time()
    make_loose(tmp_path, "old-build", mtime=now - 10_000)
    make_loose(tmp_path, "new-build", mtime=now)
    found = sources.discover(tmp_path)
    assert [s.path.name for s in found] == ["new-build", "old-build"]


def test_name_breaks_ties_for_equal_mtime(tmp_path):
    stamp = time.time() - 500
    make_loose(tmp_path, "zebra", mtime=stamp)
    make_loose(tmp_path, "alpha", mtime=stamp)
    assert [s.path.name for s in sources.discover(tmp_path)] == ["alpha", "zebra"]


def test_a_folder_that_is_itself_firmware_is_returned(tmp_path):
    d = make_loose(tmp_path, "just-this")
    found = sources.discover(d)
    assert [s.path for s in found] == [d]


def test_empty_root_yields_nothing(tmp_path):
    assert sources.discover(tmp_path) == []


def test_missing_root_is_not_an_error(tmp_path):
    assert sources.discover(tmp_path / "nope") == []


# ------------------------------------------------------------ classification --
def test_staged_folder_is_trusted_and_carries_provenance(tmp_path):
    make_staged(tmp_path, "staged", git_sha="deadbeefcafe")
    s = sources.discover(tmp_path)[0]
    assert s.kind == "manifest" and s.trusted
    assert s.git_sha == "deadbeefcafe"
    assert s.boards == ("powerstage",)


def test_loose_folder_is_untrusted_and_says_why(tmp_path):
    make_loose(tmp_path, "drop")
    s = sources.discover(tmp_path)[0]
    assert s.kind == "loose" and not s.trusted
    assert any("cannot be checksum-verified" in n for n in s.notes)


def test_dirty_staged_folder_is_flagged(tmp_path):
    make_staged(tmp_path, "staged", dirty=True)
    s = sources.discover(tmp_path)[0]
    assert s.git_dirty and any("dirty" in n for n in s.notes)


def test_corrupt_manifest_degrades_to_loose(tmp_path):
    d = make_loose(tmp_path, "broken")
    (d / "manifest.json").write_text("{not json", encoding="utf-8")
    s = sources.discover(tmp_path)[0]
    assert s.kind == "loose"
    assert any("unreadable" in n for n in s.notes)


def test_config_file_is_detected(tmp_path):
    d = make_loose(tmp_path, "withcfg")
    (d / "fabrica.json").write_text("{}", encoding="utf-8")
    assert sources.discover(tmp_path)[0].config.name == "fabrica.json"


# ------------------------------------------------------- name attribution --
@pytest.mark.parametrize("name,board,kind", [
    ("powerstage_app.srec", "powerstage", "app"),
    ("G0B1_PowerStage_Boot.srec", "powerstage", "boot"),
    ("Actuation_IO_Distribution_Board_Embedded.srec", "kincodrive", "app"),
    ("G0B1_KincoDrive_Boot.srec", "kincodrive", "boot"),
    ("STM32G0_LEDDRIVER_PROG.srec", "leddriver", "app"),
    ("Fabrica_STM32F3RE_Boot.srec", "knob", "boot"),
    ("Fabrica_STM32F3_Prog.srec", "knob", "app"),
    ("something_random.srec", None, "app"),
])
def test_board_inferred_from_real_artifact_names(name, board, kind):
    """These are the names the build actually produces."""
    assert sources.board_from_filename(name) == (board, kind)


# -------------------------------------------------------------------- load --
def test_loading_a_staged_folder_uses_its_manifest(tmp_path):
    make_staged(tmp_path, "staged")
    man = sources.load(sources.discover(tmp_path)[0])
    assert man.board("powerstage").blt_rx == 0x130
    assert man.gate == "pass"


def test_loading_a_loose_folder_synthesises_a_usable_manifest(tmp_path):
    make_loose(tmp_path, "drop", boards=("powerstage", "leddriver"))
    man = sources.load(sources.discover(tmp_path)[0])
    assert set(man.board_ids) == {"powerstage", "leddriver"}
    assert man.board("leddriver").blt_rx == 0x160
    assert man.board("powerstage").app.load_addr == "0x08003000"


def test_synthesised_manifest_declares_itself_unverified(tmp_path):
    """Downstream code already surfaces gate/dirty, so it must be honest here."""
    make_loose(tmp_path, "drop")
    man = sources.load(sources.discover(tmp_path)[0])
    assert man.gate == "unverified"
    assert man.git_dirty is True


def test_synthesised_checksums_are_self_consistent(tmp_path):
    """They prove nothing about provenance, but must match the files present."""
    make_loose(tmp_path, "drop")
    man = sources.load(sources.discover(tmp_path)[0])
    assert mf.verify_all(man) == []


def test_knob_gets_its_own_load_address(tmp_path):
    make_loose(tmp_path, "drop", boards=("knob",))
    man = sources.load(sources.discover(tmp_path)[0])
    assert man.board("knob").app.load_addr == "0x08003800"
    assert man.board("knob").blt_rx == 0x667


def test_config_overrides_the_defaults(tmp_path):
    d = make_loose(tmp_path, "drop")
    (d / "fabrica.json").write_text(json.dumps(
        {"powerstage": {"blt_rx": "0x230", "blt_tx": "0x231", "bitrate": 250000}}),
        encoding="utf-8")
    man = sources.load(sources.discover(tmp_path)[0])
    assert man.board("powerstage").blt_rx == 0x230
    assert man.board("powerstage").bitrate == 250000


def test_board_with_only_one_image_is_omitted_and_explained(tmp_path):
    d = tmp_path / "half"
    make_srec(d / "powerstage_boot.srec")
    src = sources.discover(tmp_path)[0]
    with pytest.raises(mf.ManifestError, match="no complete board"):
        sources.load(src)
    assert any("no app image" in n for n in src.notes)
