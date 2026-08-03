"""Flash a bootloader over ST-Link V3, via whichever backend the bench has.

Written before first contact with the hardware, so the design rule is: every
byte of argv is decided by pure functions that can be tested on a laptop with no
probe attached, and exactly one function actually spawns a process. If Monday's
bench session finds a wrong flag, the fix is in `build_command` and the test that
pins it, not scattered through the flashing logic.

The three backends disagree about the one thing that matters most here -- who
decides the load address:

  STM32_Programmer_CLI  reads .srec/.hex/.elf and takes the address FROM the
                        file. Passing an address alongside a .srec is how you
                        write a 0x08003000 application to 0x08000000.
  openocd               same, but only if it recognises the file format, and it
                        infers format from the file EXTENSION (see below).
  st-flash              has no format parser worth relying on; it writes raw
                        bytes at an address you supply.

Every image in firmware/manifest.json is a .srec, which is the awkward case for
two of the three backends. See OPENOCD_* and the st-flash branch below.
"""
from __future__ import annotations

import shlex
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

from .env import STLINK_BACKENDS, find_stlink  # noqa: F401  (find_stlink re-exported)

__all__ = [
    "FlashResult",
    "build_command",
    "flash",
    "stream_output",
    "format_command",
    "openocd_target_cfg",
    "parse_openocd_version",
    "openocd_supports_mcu",
    "OPENOCD_MIN_VERSION",
    "STLINK_BACKENDS",
    "find_stlink",
]

# --- image format classification -----------------------------------------
# Motorola S-record, under every name our toolchains emit it as.
SREC_SUFFIXES = (".srec", ".s19", ".s28", ".s37", ".sre", ".mot")
IHEX_SUFFIXES = (".hex", ".ihex")
ELF_SUFFIXES = (".elf", ".out", ".axf")

# --- openocd -------------------------------------------------------------
OPENOCD_INTERFACE_CFG = "interface/stlink.cfg"

# MCU prefix -> target config. Deliberately narrow: a wrong target cfg gives you
# a wrong flash geometry, and a wrong flash geometry gives you a half-erased
# board. Add a line here when a new board joins the fleet; do not pattern-match
# loosely on "STM32G0" and hope.
OPENOCD_TARGET_CFGS: dict[str, str] = {
    "STM32G0B1": "target/stm32g0x.cfg",
    "STM32F303": "target/stm32f3x.cfg",
}

# openocd picks its image parser from the file extension, and ".srec" is not one
# of the extensions it knows (it knows .s19). An unrecognised extension is
# treated as a raw binary, which would write the ASCII text of the S-record to
# the load address -- a silent, total corruption rather than an error. The
# `program` helper cannot be told a format, so for S-records we drive
# flash write_image / verify_image directly and name the format explicitly.
OPENOCD_SRECORD_TYPE = "s19"

# Minimum openocd version that can program each MCU family, as (major, minor).
#
# Learned on the bench, not from the docs: openocd 0.11.0 -- the newest in
# Ubuntu 22.04 (jammy/universe) -- attaches to an STM32G0B1 perfectly well and
# can read its memory, but its stm32l4x flash driver (which covers G0) has no
# entry for device id 0x467. Flashing dies at
#
#   Warn : Cannot identify target as an STM32G0/G4/L4/L4+/L5/WB/WL family device.
#   Error: auto_probe failed
#
# after the core has already been halted, which looks exactly like a dead
# board. 0x467 support landed in 0.12.0. The F303 goes through stm32f1x and
# has worked since long before 0.11, hence no entry for it.
OPENOCD_MIN_VERSION: dict[str, tuple[int, int]] = {
    "STM32G0B1": (0, 12),
}


@dataclass
class FlashResult:
    ok: bool
    returncode: int
    output: str
    command: list[str] = field(default_factory=list)


def _suffix(image: Path) -> str:
    return Path(image).suffix.lower()


def is_srec(image: Path) -> bool:
    return _suffix(image) in SREC_SUFFIXES


def _address_is_implied(image: Path) -> bool:
    """True when the image file itself carries the load address."""
    return _suffix(image) in SREC_SUFFIXES + IHEX_SUFFIXES + ELF_SUFFIXES


def _hex_addr(load_addr: int) -> str:
    return f"0x{load_addr:08x}"


def format_command(command: Iterable[str]) -> str:
    """Render argv the way you would paste it into a bench shell (POSIX)."""
    return shlex.join(str(part) for part in command)


def openocd_target_cfg(mcu: str) -> str:
    """Map an MCU part number onto an openocd target config.

    Raises ValueError rather than guessing: flashing the right image through the
    wrong target description is one of the few ways this tool could brick a board.
    """
    part = (mcu or "").strip().upper()
    for prefix, cfg in OPENOCD_TARGET_CFGS.items():
        if part.startswith(prefix):
            return cfg
    known = ", ".join(f"{p}*" for p in OPENOCD_TARGET_CFGS)
    raise ValueError(
        f"no openocd target config known for MCU {mcu!r}; "
        f"known prefixes: {known}. "
        f"Add it to OPENOCD_TARGET_CFGS in fabrica/stlink.py (do not guess)."
    )


def parse_openocd_version(text: str) -> tuple[int, int] | None:
    """Pull (major, minor) out of `openocd --version` output.

    The banner looks like `Open On-Chip Debugger 0.11.0` on a release build and
    `Open On-Chip Debugger 0.12.0+dev-00600-g[...]` on a self-built one, so the
    minor number has to be taken as digits-until-non-digit rather than by
    splitting the whole string on dots.
    """
    marker = "Open On-Chip Debugger"
    for line in (text or "").splitlines():
        if marker not in line:
            continue
        tail = line.split(marker, 1)[1].strip()
        number = ""
        for ch in tail:
            if ch.isdigit() or ch == ".":
                number += ch
            else:
                break
        parts = number.split(".")
        if len(parts) >= 2 and parts[0].isdigit() and parts[1].isdigit():
            return int(parts[0]), int(parts[1])
    return None


def openocd_supports_mcu(mcu: str, version: tuple[int, int] | None
                         ) -> tuple[bool, str | None]:
    """Can this openocd program this MCU? Returns (ok, reason-if-not).

    An unknown version is treated as usable: refusing to flash because we could
    not parse a banner would be worse than letting openocd speak for itself.
    """
    part = (mcu or "").strip().upper()
    for prefix, minimum in OPENOCD_MIN_VERSION.items():
        if not part.startswith(prefix):
            continue
        if version is None or version >= minimum:
            return True, None
        have = ".".join(str(n) for n in version)
        need = ".".join(str(n) for n in minimum)
        return False, (
            f"openocd {have} cannot program {prefix} (needs >= {need}); its "
            f"flash driver has no entry for this device id and fails with "
            f"'auto_probe failed' after halting the core"
        )
    return True, None


def _cube_command(backend_path: str, image: Path, load_addr: int) -> list[str]:
    """STM32CubeProgrammer CLI.

    STM32_Programmer_CLI -c port=SWD mode=UR -w <file> [addr] -v -rst

    mode=UR (connect under reset) is what makes this work on a board whose
    application has already reconfigured or disabled SWD pins -- the normal
    failure mode for a board that shipped with firmware on it.
    """
    cmd = [backend_path, "-c", "port=SWD", "mode=UR", "-w", str(image)]
    if not _address_is_implied(image):
        cmd.append(_hex_addr(load_addr))
    cmd += ["-v", "-rst"]
    return cmd


def _openocd_srec_script(image: Path) -> str:
    """Tcl that programs an S-record and ALWAYS leaves the core running.

    openocd stops executing further -c commands the moment one fails, so the
    obvious `init; reset init; write; verify; reset run` chain leaves the target
    halted whenever the write fails. A halted STM32 is silent on CAN and draws
    the same current as a running one, so the bench symptom of a failed flash is
    a board that looks bricked -- and the natural next move, power-cycling it,
    destroys the evidence.

    Catching both steps and resuming unconditionally means a failed flash leaves
    the board doing exactly what it was doing before, with the reason on stdout.
    The final `error` re-raises so openocd still exits non-zero and FlashResult
    still reports ok=False.
    """
    fmt = OPENOCD_SRECORD_TYPE
    # Braces make the path a single Tcl word without substitution, so spaces in
    # the firmware directory are safe.
    return "\n".join([
        "init",
        "reset init",
        "set fabrica_err {}",
        f"if {{[catch {{flash write_image erase {{{image}}} 0 {fmt}}} msg]}} {{",
        '    set fabrica_err "write failed: $msg"',
        "}",
        "if {$fabrica_err eq {}} {",
        f"    if {{[catch {{verify_image {{{image}}} 0 {fmt}}} msg]}} {{",
        '        set fabrica_err "verify failed: $msg"',
        "    }",
        "}",
        "# Unconditional: never hand back a halted board.",
        "catch {reset run}",
        "if {$fabrica_err ne {}} {",
        '    echo "fabrica: $fabrica_err (core resumed)"',
        "    error $fabrica_err",
        "}",
        "shutdown",
    ])


def _openocd_command(backend_path: str, image: Path, load_addr: int,
                     mcu: str) -> list[str]:
    target_cfg = openocd_target_cfg(mcu)
    cmd = [backend_path, "-f", OPENOCD_INTERFACE_CFG, "-f", target_cfg]

    if is_srec(image):
        # Explicit format; see OPENOCD_SRECORD_TYPE. Offset 0 means "use the
        # addresses recorded in the image", matching what `program` does for a
        # file with absolute addresses.
        cmd += ["-c", _openocd_srec_script(image)]
        return cmd

    program = f"program {image}"
    if not _address_is_implied(image):
        program += f" {_hex_addr(load_addr)}"
    program += " verify reset exit"
    cmd += ["-c", program]
    return cmd


def _st_flash_command(backend_path: str, image: Path, load_addr: int) -> list[str]:
    """stlink-tools.

    st-flash writes raw bytes at an address. It can also parse Intel hex
    (--format=ihex) but it has no S-record parser at all -- handing it a .srec
    writes the ASCII text of the file to flash.
    """
    if is_srec(image):
        # Rebuilt textually, not via Path.with_suffix: the message is a command
        # to paste on the Ubuntu/Jetson bench, and must not pick up Windows
        # separators when the tool is developed on the laptop.
        text = str(image)
        converted = text[: len(text) - len(_suffix(image))] + ".bin"
        raise ValueError(
            f"st-flash cannot program S-record files ({text}); it has no "
            "S-record parser and would write the file's ASCII text to flash. "
            "Convert first:\n"
            f"  arm-none-eabi-objcopy -I srec -O binary {text} {converted}\n"
            "then flash the .bin at its load address, or use "
            "STM32_Programmer_CLI / openocd instead."
        )
    if _suffix(image) in ELF_SUFFIXES:
        raise ValueError(
            f"st-flash cannot program ELF files ({image}); convert with "
            f"objcopy -O binary, or use STM32_Programmer_CLI / openocd."
        )
    if _suffix(image) in IHEX_SUFFIXES:
        # Address comes from the hex records, and st-flash rejects an address
        # argument in this mode.
        return [backend_path, "--format=ihex", "write", str(image)]
    return [backend_path, "write", str(image), _hex_addr(load_addr)]


def build_command(backend: str, backend_path: str, image: Path,
                  load_addr: int, mcu: str) -> list[str]:
    """Construct argv for one flash operation. Pure: touches no hardware, no disk.

    Raises ValueError for an unknown backend, an MCU with no openocd target
    mapping, or an image format the chosen backend cannot parse.
    """
    if backend == "STM32_Programmer_CLI":
        return _cube_command(backend_path, image, load_addr)
    if backend == "openocd":
        return _openocd_command(backend_path, image, load_addr, mcu)
    if backend == "st-flash":
        return _st_flash_command(backend_path, image, load_addr)
    raise ValueError(
        f"unknown ST-Link backend {backend!r}; expected one of: "
        f"{', '.join(STLINK_BACKENDS)}"
    )


def stream_output(proc, on_output: Callable[[str], None] | None = None) -> str:
    """Drain proc.stdout line by line, echoing each line as it arrives.

    The TUI needs progress while a 45 kB application is being written, not one
    blob at the end. Lines are handed over without their trailing newline.
    """
    chunks: list[str] = []
    stdout = getattr(proc, "stdout", None)
    if stdout is not None:
        for line in stdout:
            chunks.append(line)
            if on_output is not None:
                on_output(line.rstrip("\r\n"))
    return "".join(chunks)


def _subprocess_runner(command: list[str],
                       on_output: Callable[[str], None] | None = None
                       ) -> tuple[int, str]:
    """The one place in this module that starts a process."""
    try:
        proc = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
    except OSError as exc:
        # Missing binary, no permission on the USB device, etc. A flash failure
        # is a result, not an exception -- the TUI shows it next to the board.
        message = f"failed to start {command[0]!r}: {exc}"
        if on_output is not None:
            on_output(message)
        return 127, message
    output = stream_output(proc, on_output)
    returncode = proc.wait()
    return returncode, output


def flash(backend: str, backend_path: str, image: Path, load_addr: int,
          mcu: str, dry_run: bool = False,
          on_output: Callable[[str], None] | None = None,
          runner: Callable | None = None) -> FlashResult:
    """Flash `image` to a board over ST-Link.

    `runner(command, on_output) -> (returncode, output)` is the only seam that
    touches the outside world; tests inject their own. With dry_run=True nothing
    is executed at all and the result reports the command that would have run.

    Raises ValueError (from build_command) before anything is executed if the
    backend, MCU, or image format is unusable.
    """
    command = build_command(backend, backend_path, image, load_addr, mcu)

    if dry_run:
        message = f"DRY RUN: would execute: {format_command(command)}"
        if on_output is not None:
            on_output(message)
        return FlashResult(ok=True, returncode=0, output=message,
                           command=list(command))

    run = runner if runner is not None else _subprocess_runner
    returncode, output = run(command, on_output)
    return FlashResult(ok=(returncode == 0), returncode=returncode,
                       output=output, command=list(command))
