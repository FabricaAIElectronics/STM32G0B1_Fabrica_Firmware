"""Tests for the flash size gate."""
from vv.checks import size


def test_over_limit_fails():
    got = size.check_artifact("boot", flash_bytes=13000, limit=12288)
    assert got["status"] == "fail"


def test_above_eighty_percent_warns():
    got = size.check_artifact("boot", flash_bytes=10500, limit=12288)
    assert got["status"] == "warn"
    assert 80 <= got["pct"] < 100


def test_comfortably_under_passes():
    # Real measured F303 bootloader: 7376 B in its 14 KB reservation = 51.4%.
    got = size.check_artifact("boot", flash_bytes=7376, limit=14336)
    assert got["status"] == "pass"
    assert got["pct"] < 80


def test_real_g0b1_bootloader_size_warns():
    """The G0B1 bootloaders really are at ~82% of their 12 KB reservation.

    This is not a hypothetical: 10136 B of 12288 B is 82.5%, so the gate warns
    on every run today. Kept as a test so that if someone later shrinks the
    bootloader or enlarges the reservation, this fails and prompts an update.
    """
    got = size.check_artifact("boot", flash_bytes=10136, limit=12288)
    assert got["status"] == "warn"
    assert 82 <= got["pct"] < 83


def test_exactly_at_limit_warns_rather_than_passing_silently():
    got = size.check_artifact("boot", flash_bytes=12288, limit=12288)
    assert got["status"] == "warn"  # 100% is not over, but must not be silent


def test_run_aggregates_worst_status(monkeypatch):
    monkeypatch.setattr(size, "gather_sizes", lambda: [
        {"artifact": "a", "flash": 100, "limit": 1000},
        {"artifact": "b", "flash": 2000, "limit": 1000},
    ])
    assert size.run().status == "fail"
