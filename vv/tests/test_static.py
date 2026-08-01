"""Tests for the static analysis stage and its warning baseline."""
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


def test_new_warning_fails_the_stage(monkeypatch):
    monkeypatch.setattr(static, "load_baseline", lambda: set())
    monkeypatch.setattr(static, "collect_warnings", lambda: ["a.c:1:1: warning: new thing"])
    result = static.run()
    assert result.status == "fail"
    assert "1 new" in result.detail


def test_baselined_warning_does_not_fail(monkeypatch):
    warn = "a.c:1:1: warning: known thing"
    monkeypatch.setattr(static, "load_baseline", lambda: {warn})
    monkeypatch.setattr(static, "collect_warnings", lambda: [warn])
    result = static.run()
    assert result.status == "pass"


def test_disappeared_baseline_entry_warns_but_does_not_fail(monkeypatch):
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
