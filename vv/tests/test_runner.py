"""Tests for the gate runner and result reporting."""
from vv.result import StageResult, format_summary
from vv.run_gate import run_stages


def _stage(name, status):
    def fn():
        return StageResult(name=name, status=status, detail=f"{name} {status}", items=[])
    return fn


def test_failfast_stops_at_first_failure():
    results = run_stages(
        [_stage("a", "pass"), _stage("b", "fail"), _stage("c", "pass")],
        continue_on_fail=False,
    )
    assert [r.name for r in results] == ["a", "b"]


def test_continue_runs_every_stage():
    results = run_stages(
        [_stage("a", "pass"), _stage("b", "fail"), _stage("c", "pass")],
        continue_on_fail=True,
    )
    assert [r.name for r in results] == ["a", "b", "c"]


def test_warn_does_not_stop_the_run():
    results = run_stages(
        [_stage("a", "warn"), _stage("b", "pass")], continue_on_fail=False
    )
    assert [r.name for r in results] == ["a", "b"]


def test_stage_exception_becomes_a_failure():
    def boom():
        raise RuntimeError("kaboom")

    results = run_stages([boom], continue_on_fail=False)
    assert results[0].status == "fail"
    assert "kaboom" in results[0].detail


def test_summary_lists_every_stage_and_status():
    text = format_summary([
        StageResult("static", "pass", "0 new warnings", []),
        StageResult("size", "warn", "powerstage at 82%", []),
    ])
    assert "static" in text and "PASS" in text
    assert "size" in text and "WARN" in text
