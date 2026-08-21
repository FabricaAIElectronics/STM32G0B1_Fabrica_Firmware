"""Tests for artifact staging and the manifest."""
import json
import pathlib

import pytest

from vv import stage


def test_refuses_to_stage_when_gate_failed():
    with pytest.raises(RuntimeError, match="gate did not pass"):
        stage.stage_artifacts(gate_passed=False)


def test_sha256_matches_known_value(tmp_path):
    f = tmp_path / "x.bin"
    f.write_bytes(b"abc")
    assert stage.sha256_of(f) == (
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    )


FAKE_ARTIFACTS = {
    "boot": {"file": "boot.srec", "sha256": "x", "flash_bytes": 1,
             "load_addr": "0x08000000"},
    "app": {"file": "app.srec", "sha256": "y", "flash_bytes": 2,
            "load_addr": "0x08003000"},
}


def _fake_collect(board, dest_root):
    return {kind: dict(entry, file=f"{board.id}/{entry['file']}")
            for kind, entry in FAKE_ARTIFACTS.items()}


def test_manifest_shape(monkeypatch, tmp_path):
    monkeypatch.setattr(stage, "collect_board_artifacts", _fake_collect)
    manifest, staged_dir = stage.stage_artifacts(
        gate_passed=True, version="unit-test", firmware_root=tmp_path)

    assert staged_dir == tmp_path / "unit-test"
    assert manifest["schema"] == 1
    assert manifest["gate"] == "pass"
    assert {b["id"] for b in manifest["boards"]} == {
        "kincodrive", "powerstage", "leddriver", "buttonboard", "knob"}

    ps = next(b for b in manifest["boards"] if b["id"] == "powerstage")
    assert ps["can"]["blt_rx"] == "0x130"
    assert ps["can"]["blt_tx"] == "0x131"
    assert ps["mcu"] == "STM32G0B1RET6"
    assert ps["address_plan_exempt"] is False

    knob = next(b for b in manifest["boards"] if b["id"] == "knob")
    assert knob["address_plan_exempt"] is True
    assert knob["dbc"] == "Knob.dbc"

    written = json.loads((staged_dir / "manifest.json").read_text())
    assert written == manifest


def test_staging_writes_only_where_it_was_told(monkeypatch, tmp_path):
    """A redirected stage must not touch the checkout's firmware tree.

    It used to. stage_artifacts() reassigned the module-level FIRMWARE_DIR and
    MANIFEST_PATH, so redirecting the destination was overwritten by the call
    itself and every test run dropped a manifest full of placeholder hashes
    into Tools/fabrica/firmware/. The TUI lists that folder newest-first, which
    put a fake set at the top of an operator's picker.
    """
    monkeypatch.setattr(stage, "collect_board_artifacts", _fake_collect)
    before = sorted(pathlib.Path(stage.FIRMWARE_ROOT).glob("*"))         if stage.FIRMWARE_ROOT.is_dir() else []

    _, staged_dir = stage.stage_artifacts(
        gate_passed=True, version="unit-test", firmware_root=tmp_path)

    after = sorted(pathlib.Path(stage.FIRMWARE_ROOT).glob("*"))         if stage.FIRMWARE_ROOT.is_dir() else []
    assert before == after, "staging leaked into the real firmware tree"
    assert (staged_dir / "manifest.json").is_file()


def test_version_label_rejects_a_path(tmp_path):
    with pytest.raises(ValueError, match="names a folder"):
        stage.stage_artifacts(gate_passed=True, version="a/b",
                              firmware_root=tmp_path)


def test_manifest_records_dirty_tree(monkeypatch):
    monkeypatch.setattr(stage, "git_info", lambda: ("deadbeef", True))
    assert stage.build_manifest([], gate_passed=True)["git_dirty"] is True
