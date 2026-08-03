"""Tests for artifact staging and the manifest."""
import json

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


def test_manifest_shape(monkeypatch, tmp_path):
    monkeypatch.setattr(stage, "FIRMWARE_DIR", tmp_path)
    monkeypatch.setattr(stage, "MANIFEST_PATH", tmp_path / "manifest.json")
    monkeypatch.setattr(stage, "collect_board_artifacts",
                        lambda b: {"boot": {"file": f"{b.id}/boot.srec", "sha256": "x",
                                            "flash_bytes": 1, "load_addr": "0x08000000"},
                                   "app": {"file": f"{b.id}/app.srec", "sha256": "y",
                                           "flash_bytes": 2, "load_addr": "0x08003000"}})
    manifest = stage.stage_artifacts(gate_passed=True)

    assert manifest["schema"] == 1
    assert manifest["gate"] == "pass"
    assert {b["id"] for b in manifest["boards"]} == {
        "kincodrive", "powerstage", "leddriver", "knob"}

    ps = next(b for b in manifest["boards"] if b["id"] == "powerstage")
    assert ps["can"]["blt_rx"] == "0x130"
    assert ps["can"]["blt_tx"] == "0x131"
    assert ps["mcu"] == "STM32G0B1RET6"
    assert ps["address_plan_exempt"] is False

    knob = next(b for b in manifest["boards"] if b["id"] == "knob")
    assert knob["address_plan_exempt"] is True
    assert knob["dbc"] == "Knob.dbc"

    written = json.loads((tmp_path / "manifest.json").read_text())
    assert written == manifest


def test_manifest_records_dirty_tree(monkeypatch):
    monkeypatch.setattr(stage, "git_info", lambda: ("deadbeef", True))
    assert stage.build_manifest([], gate_passed=True)["git_dirty"] is True
