"""Tests for the host unit-test stage wrapper."""
import json

from vv.unit import runner


def test_parses_pass_and_fail_counts():
    out = "TEST PASS can_layout_powerstage_cmd_oc\nTEST FAIL can_layout_knob\n1 failed\n"
    summary = runner.parse_output(out)
    assert summary["passed"] == 1
    assert summary["failed"] == 1
    assert summary["failures"] == ["can_layout_knob"]


def test_all_passing_gives_pass(monkeypatch):
    monkeypatch.setattr(runner, "run_make", lambda: (0, "TEST PASS a\nTEST PASS b\n"))
    assert runner.run().status == "pass"


def test_any_failure_gives_fail(monkeypatch):
    monkeypatch.setattr(runner, "run_make", lambda: (1, "TEST PASS a\nTEST FAIL b\n"))
    result = runner.run()
    assert result.status == "fail"
    assert "b" in result.detail


def test_layouts_json_is_valid_after_a_real_run(tmp_path):
    """Guards the contract Task 9 depends on."""
    path = runner.LAYOUTS_PATH
    if not path.is_file():
        import pytest
        pytest.skip("layouts.json not generated yet; run make in vv/unit first")
    data = json.loads(path.read_text())
    assert "boards" in data
    for board_id, msgs in data["boards"].items():
        for m in msgs:
            assert {"id", "name", "dlc", "byte_order"} <= set(m)
            assert m["byte_order"] in ("big", "little")
