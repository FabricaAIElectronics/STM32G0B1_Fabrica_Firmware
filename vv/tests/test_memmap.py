"""Tests for the .srec memory-map stage.

The real artifacts do not overlap, so the overlap case is built synthetically.
A check that has never seen the failure it exists to catch is not a check.
"""
import pytest

from vv.checks import memmap


#: An S-record's byte-count field is a single byte, so one line can carry at
#: most 255 - 4 address - 1 checksum bytes of data. Real tools use 16 or 32.
_MAX_DATA_PER_LINE = 16


def write_srec(path, records):
    """records: list of (address, data_bytes) -> a minimal S3 file."""
    lines = ["S00600004844521B"]
    for addr, data in records:
        for offset in range(0, len(data), _MAX_DATA_PER_LINE):
            chunk = data[offset:offset + _MAX_DATA_PER_LINE]
            count = 4 + len(chunk) + 1     # 4 address bytes + data + checksum
            body = f"{count:02X}{addr + offset:08X}" + chunk.hex().upper()
            checksum = 0xFF - (sum(bytes.fromhex(body)) & 0xFF)
            lines.append(f"S3{body}{checksum:02X}")
    lines.append("S70500000000FA")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def test_ranges_parsed_and_merged(tmp_path):
    f = tmp_path / "a.srec"
    write_srec(f, [(0x08000000, b"\x01" * 16), (0x08000010, b"\x02" * 16),
                   (0x08001000, b"\x03" * 8)])
    assert memmap.srec_ranges(f) == [(0x08000000, 0x08000020),
                                     (0x08001000, 0x08001008)]


def test_written_bytes_ignores_gaps(tmp_path):
    f = tmp_path / "a.srec"
    write_srec(f, [(0x08000000, b"\x01" * 16), (0x08001000, b"\x03" * 8)])
    assert memmap.written_bytes(memmap.srec_ranges(f)) == 24


def test_non_data_records_write_nothing(tmp_path):
    """S0 header and S7 start-address must not count as flash content."""
    f = tmp_path / "a.srec"
    f.write_text("S00600004844521B\nS70500000000FA\n", encoding="ascii")
    assert memmap.srec_ranges(f) == []


def test_overlap_detected():
    a = [(0x08000000, 0x08003100)]
    b = [(0x08003000, 0x08010000)]
    assert memmap.overlaps(a, b) == [(0x08003000, 0x08003100)]


def test_no_overlap_when_adjacent():
    """Touching but not overlapping is fine: [start, end) is half-open."""
    assert memmap.overlaps([(0x08000000, 0x08003000)],
                           [(0x08003000, 0x08010000)]) == []


def _board(tmp_path, monkeypatch, boot_records, app_records,
           reserved=12 * 1024, origin=0x08003000):
    """Build a fake board whose .srec files live under tmp_path."""
    from vv.boards import Board
    (tmp_path / "boot" / "Debug").mkdir(parents=True)
    (tmp_path / "app" / "Debug").mkdir(parents=True)
    write_srec(tmp_path / "boot" / "Debug" / "B.srec", boot_records)
    write_srec(tmp_path / "app" / "Debug" / "A.srec", app_records)
    monkeypatch.setattr(memmap, "REPO_ROOT", tmp_path)
    return Board(
        id="fake", name="Fake", mcu="STM32G0B1RET6",
        app_dir="app", app_eclipse="A", boot_dir="boot", boot_eclipse="B",
        dbc=None, headers=(), blt_rx=0x130, blt_tx=0x131, bitrate=500000,
        extended=False, boot_reserved_bytes=reserved, app_origin=origin,
        flash_total_bytes=512 * 1024, address_plan_exempt=False, in_bus_doc=True)


def test_clean_layout_has_no_problems(tmp_path, monkeypatch):
    b = _board(tmp_path, monkeypatch,
               [(0x08000000, b"\x01" * 256)], [(0x08003000, b"\x02" * 256)])
    r = memmap.check_board(b)
    assert r["present"] and r["problems"] == []
    assert r["gap"] == 0x08003000 - 0x08000100


def test_bootloader_overrunning_its_reservation_is_caught(tmp_path, monkeypatch):
    """The F303 bootloader's linker declares 512K, so only this check would notice."""
    b = _board(tmp_path, monkeypatch,
               [(0x08000000, b"\x01" * 256), (0x08003100, b"\x01" * 16)],
               [(0x08004000, b"\x02" * 256)])
    r = memmap.check_board(b)
    assert any("overruns its 12 KB reservation" in p for p in r["problems"])


def test_overlap_is_reported_loudly(tmp_path, monkeypatch):
    b = _board(tmp_path, monkeypatch,
               [(0x08000000, b"\x01" * 0x3100)], [(0x08003000, b"\x02" * 256)])
    r = memmap.check_board(b)
    assert any("OVERLAP" in p for p in r["problems"]), r["problems"]
    assert any("corrupts the other" in p for p in r["problems"])


def test_application_below_its_load_address_is_caught(tmp_path, monkeypatch):
    b = _board(tmp_path, monkeypatch,
               [(0x08000000, b"\x01" * 16)], [(0x08002000, b"\x02" * 16)])
    r = memmap.check_board(b)
    assert any("below its load address" in p for p in r["problems"])


def test_missing_artifacts_report_not_present(tmp_path, monkeypatch):
    from vv.boards import Board
    monkeypatch.setattr(memmap, "REPO_ROOT", tmp_path)
    b = Board(id="fake", name="Fake", mcu="STM32G0B1RET6", app_dir="nope",
              app_eclipse="A", boot_dir="nope", boot_eclipse="B", dbc=None,
              headers=(), blt_rx=0x130, blt_tx=0x131, bitrate=500000,
              extended=False, boot_reserved_bytes=12 * 1024,
              app_origin=0x08003000, flash_total_bytes=512 * 1024,
              address_plan_exempt=False, in_bus_doc=True)
    r = memmap.check_board(b)
    assert r["present"] is False
    assert r["problems"] == []


def test_stage_skips_when_nothing_is_built(tmp_path, monkeypatch):
    monkeypatch.setattr(memmap, "REPO_ROOT", tmp_path)
    assert memmap.run().status == "skip"
