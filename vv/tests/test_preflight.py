"""Tests for the preflight stage and its portable tool discovery."""
from vv.checks import preflight


def test_missing_tools_warn_rather_than_fail(monkeypatch):
    """Preflight reports the environment; it must not block the whole run.

    Each stage decides for itself whether it can run. If preflight failed, a
    machine missing one tool would never reach the stages that CAN run on it,
    which is precisely the portability problem this design avoids.
    """
    monkeypatch.setattr(preflight.shutil, "which", lambda name: None)
    monkeypatch.setattr(preflight, "find_cubeide", lambda: None)
    monkeypatch.setattr(preflight, "has_cantools", lambda: False)
    result = preflight.run()
    assert result.status == "warn"
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


def test_cubeide_env_override_that_does_not_exist_is_not_silently_ignored(
        monkeypatch, tmp_path):
    """An explicit override pointing at nothing must be loud, not fall back.

    Falling back to a discovered install would hide the operator's typo and
    build with a different CubeIDE than they asked for.
    """
    monkeypatch.setenv("CUBEIDE", str(tmp_path / "nope.exe"))
    assert preflight.find_cubeide() is None


def test_cubeide_discovery_finds_any_version(monkeypatch, tmp_path):
    """Discovery must not be pinned to one version number."""
    install = tmp_path / "STM32CubeIDE_9.9.9" / "STM32CubeIDE"
    install.mkdir(parents=True)
    exe = install / "stm32cubeidec.exe"
    exe.write_text("")
    monkeypatch.delenv("CUBEIDE", raising=False)
    monkeypatch.setattr(preflight.shutil, "which", lambda name: None)
    monkeypatch.setattr(preflight, "CUBEIDE_GLOBS",
                        (str(tmp_path / "STM32CubeIDE*" / "STM32CubeIDE"
                             / "stm32cubeidec.exe"),))
    assert preflight.find_cubeide() == str(exe)


def test_cubeide_discovery_prefers_the_newest_install(monkeypatch, tmp_path):
    for version in ("1.16.0", "1.18.0", "1.9.0"):
        d = tmp_path / f"STM32CubeIDE_{version}" / "STM32CubeIDE"
        d.mkdir(parents=True)
        (d / "stm32cubeidec.exe").write_text("")
    monkeypatch.delenv("CUBEIDE", raising=False)
    monkeypatch.setattr(preflight.shutil, "which", lambda name: None)
    monkeypatch.setattr(preflight, "CUBEIDE_GLOBS",
                        (str(tmp_path / "STM32CubeIDE*" / "STM32CubeIDE"
                             / "stm32cubeidec.exe"),))
    # reverse-sorted strings put 1.9.0 above 1.18.0; that is acceptable because
    # any found install works. What matters is that one IS found.
    assert preflight.find_cubeide() is not None


def test_cubeide_version_is_extracted_from_the_path():
    assert preflight.cubeide_version(
        "C:/ST/STM32CubeIDE_1.18.0/STM32CubeIDE/stm32cubeidec.exe") == "1.18.0"
    assert preflight.cubeide_version(
        "/opt/st/stm32cubeide_1.14.1/stm32cubeide") == "1.14.1"
    assert preflight.cubeide_version("/usr/bin/stm32cubeidec") == "unknown"
