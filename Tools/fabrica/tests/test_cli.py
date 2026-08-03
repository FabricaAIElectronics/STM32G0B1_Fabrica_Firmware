"""Tests for the fabrica CLI.

These run with no hardware, no ST-Link and no CAN interface, which is also the
state of the machine this tool was written on. Anything that would touch
hardware is either dry-run or monkeypatched.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOL_DIR))

import fabrica_cli as cli  # noqa: E402
from fabrica import env, manifest as mf  # noqa: E402


@pytest.fixture(autouse=True)
def _pinned_stlink_backend(monkeypatch):
    """Pin the ST-Link backend for every test in this module.

    Without this, these tests inherit whatever the machine happens to have
    installed: a box with no ST-Link falls back to STM32_Programmer_CLI and
    passes, while a box with stlink-tools picks st-flash, which cannot program
    a .srec, and the same test fails. That is an environment-dependent test,
    which is the exact failure mode this tool exists to prevent.

    Tests that care about a specific backend monkeypatch it themselves; a
    monkeypatch inside the test runs after this fixture and wins.
    """
    monkeypatch.setattr(env, "find_stlink",
                        lambda: ("STM32_Programmer_CLI",
                                 "/usr/bin/STM32_Programmer_CLI"))


@pytest.fixture
def fake_firmware(tmp_path):
    """A complete two-board firmware directory with correct checksums."""
    import hashlib

    fw = tmp_path / "firmware"
    boards = []
    for bid, mcu, rx, tx, origin in (
        ("powerstage", "STM32G0B1RET6", "0x130", "0x131", "0x08003000"),
        ("knob", "STM32F303RET6", "0x667", "0x7E1", "0x08003800"),
    ):
        (fw / bid).mkdir(parents=True)
        entry = {"id": bid, "name": bid.title(), "mcu": mcu,
                 "can": {"blt_rx": rx, "blt_tx": tx, "bitrate": 500000,
                         "extended": False},
                 "dbc": None, "address_plan_exempt": bid == "knob"}
        for kind, addr in (("boot", "0x08000000"), ("app", origin)):
            name = f"{bid}_{kind}.srec"
            data = f"S0 fake {bid} {kind}\n".encode()
            (fw / bid / name).write_bytes(data)
            entry[kind] = {"file": f"{bid}/{name}",
                           "sha256": hashlib.sha256(data).hexdigest(),
                           "flash_bytes": len(data), "load_addr": addr}
        boards.append(entry)

    (fw / "manifest.json").write_text(json.dumps({
        "schema": 1, "generated": "2026-08-01T00:00:00Z", "git_sha": "deadbeefcafe",
        "git_dirty": False, "gate": "pass", "boards": boards}), encoding="utf-8")
    return fw


def run(fake_firmware, *argv) -> tuple[int, str]:
    """Invoke the CLI and capture stdout."""
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cli.main(["--no-colour", "--firmware", str(fake_firmware), *argv])
    return rc, buf.getvalue()


# ------------------------------------------------------------------ list --
def test_list_shows_every_board_and_its_bootloader_ids(fake_firmware):
    rc, out = run(fake_firmware, "list")
    assert rc == 0
    assert "powerstage" in out and "knob" in out
    assert "0x130/0x131" in out
    assert "0x667/0x7E1" in out
    assert "deadbeef" in out


def test_list_flags_the_address_plan_exemption(fake_firmware):
    _, out = run(fake_firmware, "list")
    assert "exempt" in out


# ----------------------------------------------------------------- flash --
def test_flash_boot_dry_run_shows_command_and_executes_nothing(fake_firmware):
    rc, out = run(fake_firmware, "flash", "powerstage", "boot", "--dry-run")
    assert rc == 0
    assert "DRY RUN" in out
    assert "powerstage_boot.srec" in out
    assert "checksum verified" in out


def test_flash_app_dry_run_uses_the_boards_can_ids(fake_firmware):
    rc, out = run(fake_firmware, "flash", "powerstage", "app", "--dry-run")
    assert rc == 0
    assert "-tid=130" in out and "-rid=131" in out
    assert "-d=can0" in out and "-b=500000" in out
    # must match the team's proven flash_can.sh invocation
    assert "-s=xcp" in out and "-t=xcp_can" in out


def test_flash_app_dry_run_honours_a_custom_interface(fake_firmware):
    _, out = run(fake_firmware, "--iface", "can1",
                 "flash", "powerstage", "app", "--dry-run")
    assert "-d=can1" in out


def test_knob_flashes_at_its_own_load_address(fake_firmware):
    """The F303 app links at 0x08003800, not the G0B1 0x08003000."""
    man = mf.load_manifest(fake_firmware)
    assert man.board("knob").app.load_addr == "0x08003800"
    assert man.board("powerstage").app.load_addr == "0x08003000"


def test_unknown_board_is_a_clean_error(fake_firmware, capsys):
    rc, _ = run(fake_firmware, "flash", "nosuchboard", "app", "--dry-run")
    assert rc == 2


def test_corrupted_image_refuses_to_flash(fake_firmware):
    """A checksum mismatch must stop a flash, not warn and continue."""
    victim = fake_firmware / "powerstage" / "powerstage_app.srec"
    victim.write_bytes(b"tampered")
    rc, _ = run(fake_firmware, "flash", "powerstage", "app", "--dry-run")
    assert rc == 2, "a corrupted image must never reach a board"


def test_missing_image_refuses_to_flash(fake_firmware):
    (fake_firmware / "powerstage" / "powerstage_boot.srec").unlink()
    rc, _ = run(fake_firmware, "flash", "powerstage", "boot", "--dry-run")
    assert rc == 2


# ----------------------------------------------------------------- reset --
def test_reset_dry_run_shows_the_trigger_frame(fake_firmware):
    rc, out = run(fake_firmware, "reset", "powerstage", "--dry-run")
    assert rc == 0
    assert "0x130" in out and "ff 00" in out


# ---------------------------------------------------------------- doctor --
def test_doctor_reports_missing_tools_with_remedies(fake_firmware, monkeypatch):
    monkeypatch.setattr(env, "find_stlink", lambda: (None, None))
    monkeypatch.setattr(env, "find_bootcommander", lambda: None)
    monkeypatch.setattr(env, "can_interfaces", lambda: [])
    rc, out = run(fake_firmware, "doctor")
    assert rc == 1
    assert "FAIL" in out
    assert "install" in out.lower()


def test_doctor_passes_when_everything_is_present(fake_firmware, monkeypatch):
    monkeypatch.setattr(env, "find_stlink",
                        lambda: ("STM32_Programmer_CLI", "/usr/bin/x"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "500000"})
    rc, out = run(fake_firmware, "doctor")
    assert rc == 0
    assert "usable" in out


def test_doctor_warns_about_a_dirty_manifest(fake_firmware, monkeypatch):
    path = fake_firmware / "manifest.json"
    data = json.loads(path.read_text())
    data["git_dirty"] = True
    path.write_text(json.dumps(data), encoding="utf-8")
    monkeypatch.setattr(env, "find_stlink", lambda: ("openocd", "/usr/bin/openocd"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "500000"})
    _, out = run(fake_firmware, "doctor")
    assert "DIRTY" in out.upper()


def test_doctor_flags_a_bitrate_mismatch(fake_firmware, monkeypatch):
    monkeypatch.setattr(env, "find_stlink", lambda: ("openocd", "/usr/bin/openocd"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/bc")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "250000"})
    rc, out = run(fake_firmware, "doctor")
    assert rc == 1
    assert "250000" in out and "CANBusSetup" in out


# -------------------------------------------------------------- manifest --
def test_missing_manifest_gives_an_actionable_error(tmp_path):
    rc = cli.main(["--no-colour", "--firmware", str(tmp_path), "list"])
    assert rc == 2


def test_wrong_schema_is_rejected(tmp_path):
    fw = tmp_path / "firmware"
    fw.mkdir()
    (fw / "manifest.json").write_text('{"schema": 99, "boards": []}', encoding="utf-8")
    with pytest.raises(mf.ManifestError, match="schema"):
        mf.load_manifest(fw)


def test_doctor_fails_when_only_st_flash_is_available(fake_firmware, monkeypatch):
    """A green doctor followed by a first-flash failure is the worst order.

    Every image this project ships is a .srec and st-flash has no S-record
    parser, so st-flash alone is not a usable backend.
    """
    monkeypatch.setattr(env, "find_stlink", lambda: ("st-flash", "/usr/bin/st-flash"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "500000"})
    rc, out = run(fake_firmware, "doctor")
    assert rc == 1
    assert "st-flash" in out
    assert "STM32CubeProgrammer" in out


def test_doctor_accepts_cubeprogrammer(fake_firmware, monkeypatch):
    monkeypatch.setattr(env, "find_stlink",
                        lambda: ("STM32_Programmer_CLI", "/usr/bin/STM32_Programmer_CLI"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "500000"})
    rc, _ = run(fake_firmware, "doctor")
    assert rc == 0


def test_backend_capability_error_is_reported_cleanly(fake_firmware, monkeypatch, capsys):
    """A ValueError from build_command must not surface as a traceback."""
    monkeypatch.setattr(env, "find_stlink", lambda: ("st-flash", "/usr/bin/st-flash"))
    rc = cli.main(["--no-colour", "--firmware", str(fake_firmware),
                   "flash", "powerstage", "boot", "--dry-run"])
    assert rc == 2
    err = capsys.readouterr().err
    assert "cannot build the flash command" in err
    assert "Traceback" not in err


def test_monitor_without_a_board_needs_no_manifest(tmp_path, monkeypatch):
    """Watching the bus is the first thing you do on a fresh bench.

    Requiring a manifest for plain listening made `monitor` fail on a machine
    where no firmware had been staged yet - which is exactly when you most want
    to see whether the board is saying anything.
    """
    import types
    from fabrica import canbus

    class FakeBus:
        def recv(self, timeout=0.0):
            return None

        def shutdown(self):
            pass

    monkeypatch.setattr(canbus, "open_bus", lambda iface: FakeBus())
    # tmp_path holds no manifest.json at all
    rc = cli.main(["--no-colour", "--firmware", str(tmp_path),
                   "monitor", "--seconds", "0.2"])
    assert rc == 1          # "no traffic seen", not a manifest error


def test_doctor_accepts_a_loose_firmware_folder(tmp_path, monkeypatch):
    """A folder of .srec images with no manifest is still usable firmware.

    doctor used to report FAIL on it while flash, list and the TUI all accepted
    it - so the documented "get doctor green before flashing" workflow could
    never be satisfied with a loose drop.
    """
    d = tmp_path / "knob"
    d.mkdir()
    (d / "knob_boot.srec").write_bytes(b"S0 boot\n")
    (d / "knob_app.srec").write_bytes(b"S0 app\n")
    monkeypatch.setattr(env, "find_stlink", lambda: ("openocd", "/usr/bin/openocd"))
    monkeypatch.setattr(env, "find_bootcommander", lambda: "/usr/local/bin/BootCommander")
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda i: {"present": True, "up": True, "bitrate": "500000"})
    e = env.doctor(d, iface="can0", bitrate=500000)
    firmware = [c for c in e.checks if c.name == "firmware"][0]
    assert firmware.status == env.OK, firmware.detail


def test_tui_refuses_without_a_terminal_instead_of_a_curses_traceback(capsys, monkeypatch):
    """Over a pipe, curses.wrapper dies with 'cbreak() returned ERR' and then
    raises 'nocbreak() returned ERR' on top, so the traceback names neither the
    cause nor the fix. Every other subcommand works without a tty, which makes
    that read as a bug in the tool rather than a missing -t."""
    from fabrica import tui

    monkeypatch.setattr(tui.sys.stdin, "isatty", lambda: False, raising=False)

    def exploding_wrapper(*a, **k):
        raise AssertionError("curses must not be entered without a terminal")

    monkeypatch.setattr(tui.curses, "wrapper", exploding_wrapper)

    rc = tui.run_tui(None, "can0", 500000)
    assert rc == 2
    err = capsys.readouterr().err
    assert "interactive terminal" in err
    assert "ssh -t" in err


# --- ergonomics: bare invocation and auto-detection ------------------------

def test_bare_invocation_opens_the_tui(monkeypatch):
    """`fab` with no arguments is the human entry point.

    Making an operator type a subcommand to reach the interface built for them
    is backwards; scripts always name one explicitly, so nothing else regresses.
    """
    seen = {}
    monkeypatch.setattr(cli, "cmd_tui", lambda a: seen.setdefault("tui", a) and 0
                        or seen.setdefault("called", True) or 0)
    cli.main([])
    assert "tui" in seen


def test_global_flags_alone_still_open_the_tui(monkeypatch):
    """`fab --iface can1` must not be an argparse error."""
    seen = {}
    monkeypatch.setattr(cli, "cmd_tui", lambda a: seen.setdefault("iface", a.iface) and 0 or 0)
    cli.main(["--iface", "can1"])
    assert seen.get("iface") == "can1"


def test_help_alone_does_not_launch_the_tui():
    """-h must print help, not open a full-screen interface."""
    with pytest.raises(SystemExit) as exc:
        cli.main(["--help"])
    assert exc.value.code == 0


def test_iface_autodetects_the_interface_that_is_up(monkeypatch):
    from fabrica import env
    monkeypatch.setattr(env, "can_interfaces", lambda: ["can0", "can1"])
    monkeypatch.setattr(env, "can_link_state",
                        lambda n: {"up": n == "can1"})
    assert cli._autodetect_iface() == "can1"


def test_iface_falls_back_to_can0_when_nothing_is_up(monkeypatch):
    from fabrica import env
    monkeypatch.setattr(env, "can_interfaces", lambda: [])
    # Naming a real interface beats reporting None in the error message.
    assert cli._autodetect_iface() == "can0"


def test_missing_firmware_names_every_directory_it_tried(tmp_path, monkeypatch):
    # Point the search at directories that do not exist, so the test does not
    # depend on whether this machine happens to have staged firmware.
    monkeypatch.setattr(cli, "_firmware_candidates",
                        lambda: [tmp_path / "a", tmp_path / "b"])

    class Args:
        firmware = None
    with pytest.raises(mf.ManifestError) as exc:
        cli._resolve_firmware(Args())
    msg = str(exc.value)
    assert "Looked in" in msg
    assert "run_gate.py --stage-artifacts" in msg


def test_subcommand_list_matches_the_parser():
    """SUBCOMMANDS drives the bare-invocation check, so drift would make a real
    subcommand silently open the TUI instead of running."""
    parser = cli.build_parser()
    choices = set()
    for action in parser._actions:
        if getattr(action, "choices", None) and hasattr(action, "_name_parser_map"):
            choices = set(action.choices)
    assert choices == set(cli.SUBCOMMANDS)
