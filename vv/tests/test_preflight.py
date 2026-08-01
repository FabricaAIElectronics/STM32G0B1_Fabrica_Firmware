"""Tests for the preflight stage."""
from vv.checks import preflight


def test_missing_tool_fails_and_names_it(monkeypatch):
    monkeypatch.setattr(preflight.shutil, "which", lambda name: None)
    monkeypatch.setattr(preflight, "find_cubeide", lambda: None)
    monkeypatch.setattr(preflight, "has_cantools", lambda: False)
    result = preflight.run()
    assert result.status == "fail"
    assert "gcc" in result.detail
    assert any("install" in item for item in result.items[0])


def test_all_present_passes(monkeypatch):
    monkeypatch.setattr(preflight.shutil, "which", lambda name: f"/usr/bin/{name}")
    monkeypatch.setattr(preflight, "find_cubeide", lambda: "C:/ST/stm32cubeidec.exe")
    monkeypatch.setattr(preflight, "has_cantools", lambda: True)
    result = preflight.run()
    assert result.status == "pass"


def test_cubeide_env_override(monkeypatch, tmp_path):
    exe = tmp_path / "stm32cubeidec.exe"
    exe.write_text("")
    monkeypatch.setenv("CUBEIDE", str(exe))
    assert preflight.find_cubeide() == str(exe)


def test_cubeide_env_override_ignored_when_missing(monkeypatch, tmp_path):
    monkeypatch.setenv("CUBEIDE", str(tmp_path / "nope.exe"))
    monkeypatch.setattr(preflight, "DEFAULT_CUBEIDE", str(tmp_path / "also-nope.exe"))
    assert preflight.find_cubeide() is None
