"""Argv-level tests for the ST-Link flashing backends.

These run on a laptop with no probe attached. Nothing here starts a real flash;
the single test that touches subprocess deliberately execs a path that does not
exist, to prove the runner turns a launch failure into a FlashResult instead of
an exception.

Paths are compared through str(IMAGE) rather than hard-coded POSIX strings so the
suite is green both on the Windows dev box and on the Ubuntu/Jetson bench.
"""
from __future__ import annotations

from pathlib import Path, PurePosixPath

import pytest

from fabrica.stlink import (
    FlashResult,
    build_command,
    flash,
    format_command,
    build_release_command,
    openocd_supports_mcu,
    openocd_target_cfg,
    parse_openocd_version,
    release,
    stream_output,
)

CUBE = "STM32_Programmer_CLI"
CUBE_PATH = "/usr/local/bin/STM32_Programmer_CLI"
OPENOCD_PATH = "/usr/bin/openocd"
STFLASH_PATH = "/usr/bin/st-flash"

# Real images from firmware/manifest.json: everything the fleet ships is .srec.
SREC = Path("/opt/fabrica/firmware/powerstage/G0B1_PowerStage_Boot.srec")
BIN = Path("/opt/fabrica/firmware/powerstage/G0B1_PowerStage_Boot.bin")
HEX = Path("/opt/fabrica/firmware/powerstage/G0B1_PowerStage_Boot.hex")
ELF = Path("/opt/fabrica/firmware/powerstage/G0B1_PowerStage_Boot.elf")

G0B1 = "STM32G0B1RET6"
F303 = "STM32F303RET6"
BOOT_ADDR = 0x08000000
APP_ADDR = 0x08003000


class RecordingRunner:
    """Stand-in for the subprocess seam. Records calls, replays canned output."""

    def __init__(self, returncode: int = 0, lines: list[str] | None = None):
        self.returncode = returncode
        self.lines = lines if lines is not None else ["ok"]
        self.calls: list[list[str]] = []

    def __call__(self, command, on_output=None):
        self.calls.append(list(command))
        output = "".join(line + "\n" for line in self.lines)
        if on_output is not None:
            for line in self.lines:
                on_output(line)
        return self.returncode, output

    @property
    def called(self) -> bool:
        return bool(self.calls)


class FakeProc:
    """Minimal Popen stand-in for stream_output."""

    def __init__(self, lines, returncode=0):
        self.stdout = iter(lines)
        self._returncode = returncode

    def wait(self):
        return self._returncode


# --- STM32_Programmer_CLI -------------------------------------------------

def test_cube_srec_omits_address():
    """The .srec carries its own addresses; passing one would relocate the image."""
    assert build_command(CUBE, CUBE_PATH, SREC, APP_ADDR, G0B1) == [
        CUBE_PATH, "-c", "port=SWD", "mode=UR", "-w", str(SREC), "-v", "-rst",
    ]


def test_cube_hex_omits_address():
    assert build_command(CUBE, CUBE_PATH, HEX, APP_ADDR, G0B1) == [
        CUBE_PATH, "-c", "port=SWD", "mode=UR", "-w", str(HEX), "-v", "-rst",
    ]


def test_cube_elf_omits_address():
    assert "0x08003000" not in build_command(CUBE, CUBE_PATH, ELF, APP_ADDR, G0B1)


def test_cube_bin_includes_address():
    assert build_command(CUBE, CUBE_PATH, BIN, BOOT_ADDR, G0B1) == [
        CUBE_PATH, "-c", "port=SWD", "mode=UR", "-w", str(BIN), "0x08000000",
        "-v", "-rst",
    ]


def test_cube_bin_address_follows_the_file():
    """Address must come immediately after the -w operand, not at the end."""
    cmd = build_command(CUBE, CUBE_PATH, BIN, APP_ADDR, G0B1)
    assert cmd[cmd.index(str(BIN)) + 1] == "0x08003000"


def test_cube_is_mcu_agnostic():
    """CubeProgrammer autodetects the part, so an unmapped MCU must still work."""
    assert build_command(CUBE, CUBE_PATH, SREC, BOOT_ADDR, "STM32H743ZIT6")


# --- openocd --------------------------------------------------------------

def test_openocd_srec_names_the_format_explicitly():
    """openocd infers format from the extension and does not know '.srec'.

    Left to `program`, it would fall back to raw-binary and write the file's
    ASCII text to flash, so the s19 parser is selected by name.
    """
    cmd = build_command("openocd", OPENOCD_PATH, SREC, APP_ADDR, G0B1)
    assert cmd[:5] == [
        OPENOCD_PATH,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32g0x.cfg",
    ]
    script = cmd[6]
    assert f"flash write_image erase {{{SREC}}} 0 s19" in script
    assert f"verify_image {{{SREC}}} 0 s19" in script


def test_openocd_srec_never_uses_bare_program():
    cmd = build_command("openocd", OPENOCD_PATH, SREC, APP_ADDR, G0B1)
    assert not any(arg.startswith("program ") for arg in cmd)


def test_openocd_srec_resumes_the_core_even_when_the_write_fails():
    """A failed flash must not leave the target halted.

    openocd abandons the remaining -c commands after the first failure, so a
    naive write/verify/reset-run chain hands back a halted MCU: silent on CAN,
    normal current draw, indistinguishable from a dead board. Both risky steps
    are wrapped so `reset run` is unconditional.
    """
    script = build_command("openocd", OPENOCD_PATH, SREC, APP_ADDR, G0B1)[6]
    assert "catch {reset run}" in script
    # The resume must come after the write and verify, not before them.
    assert script.index("write_image") < script.index("catch {reset run}")
    assert script.index("verify_image") < script.index("catch {reset run}")
    # ...and the failure must still surface as a non-zero exit.
    assert "error $fabrica_err" in script


def test_openocd_srec_braces_the_image_path_for_spaces():
    spaced = Path("/opt/fabrica bench/firmware/app.srec")
    script = build_command("openocd", OPENOCD_PATH, spaced, APP_ADDR, G0B1)[6]
    assert f"{{{spaced}}}" in script


# --- openocd version gate -------------------------------------------------

@pytest.mark.parametrize("banner,expected", [
    ("Open On-Chip Debugger 0.11.0\nLicensed under GNU GPL v2", (0, 11)),
    ("Open On-Chip Debugger 0.12.0", (0, 12)),
    # Self-built openocd carries a git suffix straight after the version.
    ("Open On-Chip Debugger 0.12.0+dev-00600-g1a2b3c4 (2026-01-01)", (0, 12)),
    ("Open On-Chip Debugger 1.0.0-rc2", (1, 0)),
    ("something else entirely", None),
    ("", None),
])
def test_parse_openocd_version(banner, expected):
    assert parse_openocd_version(banner) == expected


def test_openocd_011_cannot_flash_g0b1():
    """The bench finding: 0.11's flash driver has no entry for device id 0x467.

    It attaches, reads memory and identifies the core, then fails at
    `auto_probe` - after halting the target.
    """
    ok, reason = openocd_supports_mcu(G0B1, (0, 11))
    assert ok is False
    assert "0.12" in reason


def test_openocd_012_can_flash_g0b1():
    assert openocd_supports_mcu(G0B1, (0, 12)) == (True, None)
    assert openocd_supports_mcu(G0B1, (1, 0)) == (True, None)


def test_openocd_011_can_still_flash_f303():
    """F303 goes through stm32f1x, supported long before 0.11 - no false alarm."""
    assert openocd_supports_mcu(F303, (0, 11)) == (True, None)


def test_unparseable_openocd_version_does_not_block_flashing():
    """Better to let openocd speak for itself than to refuse on a parse failure."""
    assert openocd_supports_mcu(G0B1, None) == (True, None)


def test_flash_refuses_g0b1_on_old_openocd_without_touching_the_board():
    """The pre-flight guard must stop before the probe is opened.

    openocd 0.11 halts the core, then fails with 'auto_probe failed' and a Tcl
    error reading 'write failed: auto erase enabled' - which names neither the
    version nor the device. Refusing up front is the difference between a
    two-minute and a thirty-minute diagnosis at the bench.
    """
    calls = []

    def runner(command, on_output=None):
        calls.append(command)
        return 0, ""

    result = flash("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, G0B1,
                   runner=runner, version_probe=lambda _p: (0, 11))
    assert result.ok is False
    assert calls == [], "openocd must not be launched at all"
    assert "0.12" in result.output


def test_flash_proceeds_on_new_enough_openocd():
    def runner(command, on_output=None):
        return 0, "ok"

    result = flash("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, G0B1,
                   runner=runner, version_probe=lambda _p: (0, 12))
    assert result.ok is True


def test_flash_version_guard_does_not_apply_to_f303():
    def runner(command, on_output=None):
        return 0, "ok"

    result = flash("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, F303,
                   runner=runner, version_probe=lambda _p: (0, 11))
    assert result.ok is True


def test_dry_run_never_probes_the_openocd_version():
    """--dry-run must stay executable on a laptop with no openocd installed."""
    def exploding_probe(_path):
        raise AssertionError("dry run must not shell out")

    result = flash("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, G0B1,
                   dry_run=True, version_probe=exploding_probe)
    assert result.ok is True


def test_openocd_hex_uses_program_without_address():
    assert build_command("openocd", OPENOCD_PATH, HEX, APP_ADDR, G0B1) == [
        OPENOCD_PATH,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32g0x.cfg",
        "-c", f"program {HEX} verify reset exit",
    ]


def test_openocd_bin_uses_program_with_address():
    assert build_command("openocd", OPENOCD_PATH, BIN, BOOT_ADDR, G0B1) == [
        OPENOCD_PATH,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32g0x.cfg",
        "-c", f"program {BIN} 0x08000000 verify reset exit",
    ]


@pytest.mark.parametrize("mcu, cfg", [
    ("STM32G0B1RET6", "target/stm32g0x.cfg"),
    ("STM32G0B1CBT6", "target/stm32g0x.cfg"),
    ("stm32g0b1ret6", "target/stm32g0x.cfg"),
    ("  STM32F303RET6 ", "target/stm32f3x.cfg"),
    ("STM32F303RET6", "target/stm32f3x.cfg"),
])
def test_openocd_target_mapping(mcu, cfg):
    assert openocd_target_cfg(mcu) == cfg
    assert cfg in build_command("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, mcu)


@pytest.mark.parametrize("mcu", [
    "STM32G071RBT6",     # different G0 sub-family: close, still not mapped
    "STM32F407VGT6",
    "STM32H743ZIT6",
    "",
    "unknown",
])
def test_openocd_unmappable_mcu_raises(mcu):
    with pytest.raises(ValueError) as exc:
        build_command("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, mcu)
    assert "target config" in str(exc.value)


def test_openocd_mcu_error_lists_known_prefixes():
    with pytest.raises(ValueError) as exc:
        openocd_target_cfg("STM32L432KC")
    assert "STM32G0B1*" in str(exc.value)
    assert "STM32F303*" in str(exc.value)


# --- st-flash -------------------------------------------------------------

def test_stflash_bin_writes_at_address():
    assert build_command("st-flash", STFLASH_PATH, BIN, BOOT_ADDR, G0B1) == [
        STFLASH_PATH, "write", str(BIN), "0x08000000",
    ]


def test_stflash_bin_uses_the_app_address_when_given_one():
    assert build_command("st-flash", STFLASH_PATH, BIN, APP_ADDR, G0B1)[-1] == \
        "0x08003000"


def test_stflash_srec_raises_and_says_how_to_convert():
    with pytest.raises(ValueError) as exc:
        build_command("st-flash", STFLASH_PATH, SREC, BOOT_ADDR, G0B1)
    message = str(exc.value)
    assert "objcopy" in message
    assert "-I srec" in message
    assert "-O binary" in message


def test_stflash_conversion_hint_keeps_the_bench_path_style():
    """The hint is pasted into an Ubuntu shell; it must stay a POSIX path."""
    image = PurePosixPath("/opt/fabrica/firmware/knob/Fabrica_STM32F3RE_Boot.srec")
    with pytest.raises(ValueError) as exc:
        build_command("st-flash", STFLASH_PATH, image, BOOT_ADDR, F303)
    message = str(exc.value)
    assert "/opt/fabrica/firmware/knob/Fabrica_STM32F3RE_Boot.bin" in message
    assert "\\" not in message


@pytest.mark.parametrize("suffix", [".srec", ".s19", ".s37", ".S19", ".mot"])
def test_stflash_rejects_every_srec_spelling(suffix):
    image = Path("/opt/fabrica/firmware/knob/Fabrica_STM32F3RE_Boot" + suffix)
    with pytest.raises(ValueError):
        build_command("st-flash", STFLASH_PATH, image, BOOT_ADDR, F303)


def test_stflash_hex_uses_ihex_format_and_no_address():
    """st-flash does parse Intel hex, and rejects an address in that mode."""
    cmd = build_command("st-flash", STFLASH_PATH, HEX, BOOT_ADDR, G0B1)
    assert cmd == [STFLASH_PATH, "--format=ihex", "write", str(HEX)]
    assert "0x08000000" not in cmd


def test_stflash_elf_raises():
    with pytest.raises(ValueError):
        build_command("st-flash", STFLASH_PATH, ELF, BOOT_ADDR, G0B1)


# --- backend selection ----------------------------------------------------

@pytest.mark.parametrize("backend", ["", "stlink", "STM32_Programmer", "jlink",
                                     "openocd ", "ST-FLASH"])
def test_unknown_backend_raises(backend):
    with pytest.raises(ValueError) as exc:
        build_command(backend, "/usr/bin/whatever", SREC, BOOT_ADDR, G0B1)
    assert "unknown ST-Link backend" in str(exc.value)


def test_unknown_backend_error_lists_the_supported_ones():
    with pytest.raises(ValueError) as exc:
        build_command("jlink", "/usr/bin/jlink", SREC, BOOT_ADDR, G0B1)
    for backend in ("STM32_Programmer_CLI", "openocd", "st-flash"):
        assert backend in str(exc.value)


# --- flash() --------------------------------------------------------------

def test_dry_run_executes_nothing():
    runner = RecordingRunner()
    result = flash(CUBE, CUBE_PATH, SREC, APP_ADDR, G0B1,
                   dry_run=True, runner=runner)
    assert runner.called is False
    assert runner.calls == []
    assert result.ok is True
    assert result.returncode == 0


def test_dry_run_reports_the_command_it_would_have_run():
    result = flash(CUBE, CUBE_PATH, SREC, APP_ADDR, G0B1, dry_run=True,
                   runner=RecordingRunner())
    assert result.command == build_command(CUBE, CUBE_PATH, SREC, APP_ADDR, G0B1)
    assert str(SREC) in result.output
    assert "DRY RUN" in result.output


def test_dry_run_still_feeds_the_progress_callback():
    lines: list[str] = []
    flash("openocd", OPENOCD_PATH, SREC, BOOT_ADDR, G0B1, dry_run=True,
          on_output=lines.append, runner=RecordingRunner())
    assert len(lines) == 1
    assert "DRY RUN" in lines[0]


def test_dry_run_validates_before_returning_ok():
    """A dry run must not bless a command that could never be built."""
    with pytest.raises(ValueError):
        flash("st-flash", STFLASH_PATH, SREC, BOOT_ADDR, G0B1,
              dry_run=True, runner=RecordingRunner())


def test_flash_passes_the_built_command_to_the_runner():
    runner = RecordingRunner()
    result = flash("openocd", OPENOCD_PATH, SREC, APP_ADDR, G0B1, runner=runner)
    expected = build_command("openocd", OPENOCD_PATH, SREC, APP_ADDR, G0B1)
    assert runner.calls == [expected]
    assert result.command == expected
    assert result.ok is True
    assert result.returncode == 0


def test_flash_success_captures_output():
    runner = RecordingRunner(0, ["Memory Programming ...", "File download complete"])
    result = flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1, runner=runner)
    assert result.ok is True
    assert "File download complete" in result.output


def test_nonzero_returncode_is_a_failure_with_output_kept():
    runner = RecordingRunner(1, ["Error: No STM32 target found!"])
    result = flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1, runner=runner)
    assert result.ok is False
    assert result.returncode == 1
    assert "No STM32 target found" in result.output
    assert result.command[0] == CUBE_PATH


@pytest.mark.parametrize("rc", [1, 2, 127, 255, -6])
def test_any_nonzero_returncode_is_not_ok(rc):
    result = flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1,
                   runner=RecordingRunner(rc))
    assert result.ok is False
    assert result.returncode == rc


def test_on_output_receives_each_line():
    seen: list[str] = []
    lines = ["Erasing memory corresponding to segment 0",
             "Download in Progress: 50%",
             "Download in Progress: 100%",
             "Verifying ..."]
    flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1,
          on_output=seen.append, runner=RecordingRunner(0, lines))
    assert seen == lines


def test_on_output_is_optional():
    result = flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1,
                   runner=RecordingRunner(0, ["quiet"]))
    assert result.ok is True


def test_flash_result_command_is_a_snapshot():
    runner = RecordingRunner()
    result = flash(CUBE, CUBE_PATH, SREC, BOOT_ADDR, G0B1, runner=runner)
    result.command.append("--mutated")
    assert runner.calls[0][-1] == "-rst"


def test_flash_raises_before_running_anything_for_a_bad_backend():
    runner = RecordingRunner()
    with pytest.raises(ValueError):
        flash("jlink", "/usr/bin/jlink", SREC, BOOT_ADDR, G0B1, runner=runner)
    assert runner.called is False


# --- stream_output --------------------------------------------------------

def test_stream_output_strips_newlines_for_the_callback_but_not_the_result():
    seen: list[str] = []
    proc = FakeProc(["one\n", "two\n"])
    output = stream_output(proc, seen.append)
    assert seen == ["one", "two"]
    assert output == "one\ntwo\n"


def test_stream_output_handles_crlf_and_no_callback():
    assert stream_output(FakeProc(["a\r\n", "b"])) == "a\r\nb"


def test_stream_output_tolerates_a_proc_with_no_stdout():
    class NoStdout:
        stdout = None

    assert stream_output(NoStdout(), lambda line: None) == ""


# --- the real subprocess seam (no hardware, no successful exec) ------------

def test_default_runner_turns_a_missing_binary_into_a_result():
    """The bench will hit this the first time a backend is not installed."""
    missing = "/nonexistent/fabrica-no-such-programmer"
    result = flash(CUBE, missing, BIN, BOOT_ADDR, G0B1)
    assert isinstance(result, FlashResult)
    assert result.ok is False
    assert result.returncode != 0
    assert missing in result.output
    assert result.command[0] == missing


# --- helpers --------------------------------------------------------------

def test_format_command_is_paste_able():
    rendered = format_command(build_command(CUBE, CUBE_PATH, BIN, BOOT_ADDR, G0B1))
    assert rendered.startswith(CUBE_PATH)
    assert "-c port=SWD mode=UR" in rendered
    assert rendered.endswith("-v -rst")


# --- release ---------------------------------------------------------------

def test_release_uses_shutdown_not_exit():
    """`exit` terminates openocd without de-initialising the adapter.

    The ST-Link can then keep the target in debug state after openocd is gone,
    which is the exact failure this command exists to undo - so it must not
    reintroduce it.
    """
    cmd = build_release_command(OPENOCD_PATH, G0B1)
    script = cmd[-1]
    assert "shutdown" in script
    assert "exit" not in script


def test_release_resumes_the_core():
    script = build_release_command(OPENOCD_PATH, G0B1)[-1]
    assert "reset run" in script
    # A hiccup resuming must not skip the detach that does the real releasing.
    assert script.index("reset run") < script.index("shutdown")


def test_release_targets_the_right_mcu_config():
    assert "target/stm32g0x.cfg" in build_release_command(OPENOCD_PATH, G0B1)
    assert "target/stm32f3x.cfg" in build_release_command(OPENOCD_PATH, F303)


def test_release_reports_failure_rather_than_raising():
    result = release(OPENOCD_PATH, G0B1, runner=lambda c, o=None: (1, "no probe"))
    assert result.ok is False


# --- bundled openocd scripts ----------------------------------------------

def test_bundled_openocd_gets_its_scripts_directory(tmp_path):
    """The binary alone cannot resolve `interface/stlink.cfg`.

    openocd searches a path compiled in at build time, so copying only the
    executable into tools/ produces a bench that passes `doctor` - the file is
    present and runs - and then exits 1 on the first flash. Proven by hiding
    /usr/local/share/openocd/scripts on the Orin.
    """
    exe = tmp_path / "openocd"
    exe.write_text("", encoding="utf-8")
    (tmp_path / "openocd-scripts").mkdir()

    cmd = build_command("openocd", str(exe), SREC, BOOT_ADDR, G0B1)
    assert "-s" in cmd
    assert str(tmp_path / "openocd-scripts") in cmd
    # The search path must precede the -f arguments it is there to resolve.
    assert cmd.index("-s") < cmd.index("-f")


def test_system_openocd_is_left_alone(tmp_path):
    """A system openocd finds its own scripts; passing -s would be noise."""
    exe = tmp_path / "openocd"
    exe.write_text("", encoding="utf-8")
    cmd = build_command("openocd", str(exe), SREC, BOOT_ADDR, G0B1)
    assert "-s" not in cmd


def test_release_also_gets_the_scripts_directory(tmp_path):
    exe = tmp_path / "openocd"
    exe.write_text("", encoding="utf-8")
    (tmp_path / "openocd-scripts").mkdir()
    cmd = build_release_command(str(exe), G0B1)
    assert "-s" in cmd
