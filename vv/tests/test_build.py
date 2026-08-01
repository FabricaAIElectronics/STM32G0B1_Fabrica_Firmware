"""Tests for the firmware build stage."""
from vv.checks import build

SIZE_LOG = """
arm-none-eabi-size  --format=berkeley "PowerStage.elf"
   text	   data	    bss	    dec	    hex	filename
  54104	     64	   4008	  58176	   e340	PowerStage.elf
Finished building: default.size.stdout
"""


def test_parse_size_extracts_sections():
    got = build.parse_size(SIZE_LOG)
    assert got == {"text": 54104, "data": 64, "bss": 4008, "elf": "PowerStage.elf"}


def test_parse_size_returns_none_when_absent():
    assert build.parse_size("nothing useful here") is None


def test_run_fails_when_a_project_fails(monkeypatch):
    monkeypatch.setattr(build, "build_project",
                        lambda d, n: {"project": n, "ok": False, "errors": ["boom"],
                                      "elf": None, "text": 0, "data": 0, "bss": 0})
    result = build.run()
    assert result.status == "fail"


def test_run_passes_when_all_projects_build(monkeypatch):
    monkeypatch.setattr(build, "build_project",
                        lambda d, n: {"project": n, "ok": True, "errors": [],
                                      "elf": f"{n}.elf", "text": 1000, "data": 0, "bss": 100})
    result = build.run()
    assert result.status == "pass"
    assert len(result.items) == 8  # 4 boards x (app + bootloader)
