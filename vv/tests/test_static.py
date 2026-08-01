"""Tests for the static analysis stage and its warning baseline."""
import pytest

from pathlib import Path

from vv.checks import static

FIXTURES = Path(__file__).parent / "fixtures"


def test_parse_warnings_extracts_only_warning_lines():
    text = FIXTURES.joinpath("warn_sample.txt").read_text()
    found = static.parse_warnings(text)
    assert len(found) == 2
    assert all("warning:" in w for w in found)


def test_normalise_makes_paths_repo_relative_with_forward_slashes(monkeypatch):
    monkeypatch.setattr(static, "REPO_ROOT", Path("C:/repo"))
    raw = r"C:\repo\STM32G0B1_Applciationprog\PowerStage\Core\Src\ui_display.c:378:22: warning: x"
    assert static.normalise(raw).startswith(
        "STM32G0B1_Applciationprog/PowerStage/Core/Src/ui_display.c:378:22:"
    )


@pytest.fixture
def toolchain_present(monkeypatch):
    """Pretend arm-none-eabi-gcc is installed.

    static.run() skips outright when it is not, so without this these tests
    pass on a machine with the toolchain and fail on one without -- which is
    exactly the environment dependence the skip logic exists to remove.
    """
    monkeypatch.setattr(static.shutil, "which", lambda name: f"/usr/bin/{name}")
    monkeypatch.setattr(static, "compiler_id", lambda: "arm-none-eabi-gcc 10.3.1")
    monkeypatch.setattr(static, "baseline_compiler", lambda: "arm-none-eabi-gcc 10.3.1")


def test_new_warning_fails_the_stage(monkeypatch, toolchain_present):
    monkeypatch.setattr(static, "load_baseline", lambda: set())
    monkeypatch.setattr(static, "collect_warnings", lambda: ["a.c:1:1: warning: new thing"])
    result = static.run()
    assert result.status == "fail"
    assert "1 new" in result.detail


def test_baselined_warning_does_not_fail(monkeypatch, toolchain_present):
    warn = "a.c:1:1: warning: known thing"
    monkeypatch.setattr(static, "load_baseline", lambda: {warn})
    monkeypatch.setattr(static, "collect_warnings", lambda: [warn])
    result = static.run()
    assert result.status == "pass"


def test_disappeared_baseline_entry_warns_but_does_not_fail(monkeypatch, toolchain_present):
    monkeypatch.setattr(static, "load_baseline", lambda: {"a.c:1:1: warning: gone now"})
    monkeypatch.setattr(static, "collect_warnings", lambda: [])
    result = static.run()
    assert result.status == "warn"
    assert "stale" in result.detail


def test_flash_layout_is_excluded_from_the_scan():
    from vv.boards import board_by_id
    names = [p.name for p in static.sources_for(board_by_id("powerstage"))]
    assert "flash_layout.c" not in names
    assert "syscalls.c" not in names
    assert "system_stm32g0xx.c" not in names


def test_stage_skips_when_the_cross_compiler_is_absent(monkeypatch):
    monkeypatch.setattr(static.shutil, "which", lambda name: None)
    result = static.run()
    assert result.status == "skip"
    assert "arm-none-eabi-gcc" in result.detail


def test_new_warnings_only_warn_when_the_compiler_changed(monkeypatch):
    """A different gcc legitimately emits a different warning set.

    Failing here would tell a new machine its firmware is broken when the only
    thing that changed is the compiler.
    """
    monkeypatch.setattr(static.shutil, "which", lambda name: f"/usr/bin/{name}")
    monkeypatch.setattr(static, "compiler_id", lambda: "arm-none-eabi-gcc 14.2.0")
    monkeypatch.setattr(static, "baseline_compiler", lambda: "arm-none-eabi-gcc 10.3.1")
    monkeypatch.setattr(static, "load_baseline", lambda: set())
    monkeypatch.setattr(static, "collect_warnings", lambda: ["a.c:1:1: warning: x"])
    result = static.run()
    assert result.status == "warn"
    assert "re-run with --update-baseline" in result.detail
