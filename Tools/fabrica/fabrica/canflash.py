"""Flash an application image over CAN with OpenBLT's BootCommander (XCP/CAN).

This is the only place in the tool that shells out to BootCommander. Everything
that touches a subprocess goes through a single injectable ``runner`` seam, so
the whole module is unit-testable with no CAN bus, no BootCommander binary and
no real process: the tests inject a fake runner and assert on the argv.

Invocation shape (mirrors the team's working ``flash_can.sh``)::

    BootCommander -s=xcp -t=xcp_can -d=<iface> -b=<bitrate> -t1=<timeout_ms> \\
                  -tid=<HEX> -rid=<HEX> <file.srec>

``-tid`` is the identifier the *host transmits* on, i.e. the board bootloader's
RX id (``blt_rx`` in the manifest). ``-rid`` is what the host receives on, i.e.
the bootloader's TX id (``blt_tx``). Both are hex, UPPERCASE, no ``0x`` prefix.

WRITTEN WITHOUT HARDWARE - verify these two things first on the bench
--------------------------------------------------------------------
1. Extended (29-bit) ids. This module flags them by ORing ``0x80000000`` into
   both identifiers, per the interface spec. The team's working shell script
   instead passes a separate ``-xid=1`` flag and leaves the ids bare. Neither
   path has ever actually been exercised: every board in ``flash_can.cfg`` and
   in the firmware manifest is 11-bit standard, so ``extended`` has always been
   0. If 29-bit is ever needed, prove which encoding BootCommander accepts
   before trusting this. Standard 11-bit flashing - the Monday critical path -
   is unaffected.
2. ``-t1=<timeout_ms>``. This is BootCommander's XCP T1 command-response
   timeout. It is NOT in the working shell script, which relies on the default
   (1000 ms). We pass it explicitly with the same 1000 ms default, so behaviour
   should be identical; if BootCommander rejects the option, drop it here.
"""
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .env import BOOTCOMMANDER_CANDIDATES, find_bootcommander

# --- BootCommander invocation constants ---------------------------------
XCP_SESSION = "xcp"
XCP_CAN_TRANSPORT = "xcp_can"
DEFAULT_TIMEOUT_MS = 1000

# --- CAN identifier constants -------------------------------------------
#: BootCommander/LibOpenBLT flag bit meaning "this is a 29-bit extended id".
EXTENDED_ID_FLAG = 0x80000000
CAN_STD_ID_MAX = 0x7FF
CAN_EXT_ID_MAX = 0x1FFFFFFF

#: The "enter bootloader" trigger every Fabrica board implements: two bytes,
#: DLC 2, sent to the board's bootloader RX id. See Docs/CAN_Bus.md.
RESET_PAYLOAD = b"\xff\x00"

# Tolerant on purpose: BootCommander's progress output has varied between
# releases (bare "45%", "  ### 45% ###", "Programming... 45%"). Match a number
# followed by an optional space and a '%' anywhere in the line.
_PERCENT_RE = re.compile(r"(\d+(?:\.\d+)?)\s*%")

#: A runner takes the argv and an optional line callback, and returns
#: ``(returncode, combined_output)``. It must never raise for an ordinary tool
#: failure - a non-zero return code is a normal result, not an exception.
Runner = Callable[[list[str], "Callable[[str], None] | None"], "tuple[int, str]"]


@dataclass
class FlashResult:
    """Outcome of one BootCommander run (or of a dry run)."""

    ok: bool
    returncode: int
    output: str
    command: list[str]


def _resolve_bootcommander(bootcommander: str | None) -> str:
    """Use the caller's path, else discover one. Raise if there is none."""
    exe = bootcommander or find_bootcommander()
    if not exe:
        raise FileNotFoundError(
            "BootCommander not found. Looked at: "
            + ", ".join(BOOTCOMMANDER_CANDIDATES)
            + "\nRun install_openblt.sh, or pass an explicit path."
        )
    return str(exe)


def _hex_id(value: int, extended: bool = False) -> str:
    """Format a CAN id the way BootCommander wants it: UPPERCASE hex, no '0x'.

    The id is emitted bare. Extended addressing is signalled by a separate
    ``-xid=1`` flag, NOT by ORing 0x80000000 into the id -- 0x80000000 is
    LibOpenBLT's internal CAN_MSG_EXT_ID_MASK at the driver layer, not the CLI
    convention. Confirmed against flash_can.sh:285, which appends -xid=1 and
    leaves -tid/-rid bare.
    """
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"CAN id must be an int, got {value!r}")
    limit = CAN_EXT_ID_MAX if extended else CAN_STD_ID_MAX
    if not 0 <= value <= limit:
        kind = "29-bit extended" if extended else "11-bit standard"
        raise ValueError(
            f"CAN id 0x{value:X} is out of range for a {kind} id "
            f"(0x0..0x{limit:X})"
        )
    return f"{value:X}"


def build_command(
    bootcommander: str,
    iface: str,
    bitrate: int,
    tid: int,
    rid: int,
    srec: Path,
    extended: bool = False,
    timeout_ms: int | None = None,
) -> list[str]:
    """Build the BootCommander argv. Pure - touches nothing but the filesystem
    lookup for BootCommander itself (skipped when ``bootcommander`` is given).

    ``tid`` is the board's bootloader RX id, ``rid`` its TX id, both as plain
    ints (e.g. ``0x130``). They are emitted as UPPERCASE hex without ``0x``.
    """
    exe = _resolve_bootcommander(bootcommander)
    if not iface:
        raise ValueError("a CAN interface name is required (e.g. 'can0')")
    if int(bitrate) <= 0:
        raise ValueError(f"bitrate must be positive, got {bitrate!r}")
    if timeout_ms is not None and int(timeout_ms) <= 0:
        raise ValueError(f"timeout_ms must be positive, got {timeout_ms!r}")

    cmd = [
        exe,
        f"-s={XCP_SESSION}",
        f"-t={XCP_CAN_TRANSPORT}",
        f"-d={iface}",
        f"-b={int(bitrate)}",
    ]
    # -t1 is BootCommander's XCP T1 timeout. flash_can.sh does not pass it and
    # 1000 ms is the documented default, so it stays opt-in: the default argv is
    # byte-for-byte the invocation the team already has working on the bench.
    if timeout_ms is not None:
        cmd.append(f"-t1={int(timeout_ms)}")
    cmd += [
        f"-tid={_hex_id(tid, extended)}",
        f"-rid={_hex_id(rid, extended)}",
    ]
    if extended:
        cmd.append("-xid=1")
    cmd.append(str(srec))
    return cmd


def run_subprocess(
    cmd: list[str],
    on_output: Callable[[str], None] | None = None,
) -> tuple[int, str]:
    """Default runner: stream BootCommander's output line by line.

    stderr is folded into stdout so the captured text reads in the order the
    operator would have seen it. Universal-newline translation turns
    BootCommander's carriage-return progress redraws into separate lines, which
    is what lets ``parse_progress`` see each update.
    """
    lines: list[str] = []
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            errors="replace",
        )
    except OSError as exc:
        # Missing binary, not executable, bad interpreter - report it the same
        # way as any other failure so the TUI has one path to render.
        message = f"failed to start {cmd[0]}: {exc}"
        if on_output is not None:
            on_output(message)
        return 127, message

    if proc.stdout is not None:
        for raw in iter(proc.stdout.readline, ""):
            line = raw.rstrip("\r\n")
            lines.append(line)
            if on_output is not None:
                on_output(line)
        proc.stdout.close()
    return proc.wait(), "\n".join(lines)


def flash(
    bootcommander: str,
    iface: str,
    bitrate: int,
    tid: int,
    rid: int,
    srec: Path,
    extended: bool = False,
    timeout_ms: int | None = None,
    dry_run: bool = False,
    on_output: Callable[[str], None] | None = None,
    runner: Callable | None = None,
) -> FlashResult:
    """Flash ``srec`` to the board answering on ``tid``/``rid``.

    ``dry_run=True`` builds and reports the command and executes nothing at all.
    ``runner`` replaces :func:`run_subprocess`; it is the only execution seam,
    so injecting it makes this function fully testable.

    Never raises on a BootCommander failure - a non-zero exit becomes
    ``ok=False`` with the output captured. It does raise ``FileNotFoundError``
    if BootCommander cannot be located, and ``ValueError`` on nonsense
    arguments, because those are operator errors, not flash failures.
    """
    command = build_command(
        bootcommander, iface, bitrate, tid, rid, srec,
        extended=extended, timeout_ms=timeout_ms,
    )

    if dry_run:
        line = "DRY RUN (nothing executed): " + " ".join(command)
        if on_output is not None:
            on_output(line)
        return FlashResult(ok=True, returncode=0, output=line, command=command)

    run = runner if runner is not None else run_subprocess
    returncode, output = run(command, on_output)
    return FlashResult(
        ok=(returncode == 0),
        returncode=returncode,
        output=output,
        command=command,
    )


def build_reset_frame(blt_rx: int) -> tuple[int, bytes]:
    """Build the "enter bootloader" frame: (arbitration id, 2-byte payload).

    Every Fabrica board listens on its bootloader RX id for ``FF 00`` (DLC 2)
    and jumps to the bootloader. Sending this is what puts a running
    application into a state BootCommander can talk to.

    The DLC is implied by ``len(payload) == 2``; the caller builds the actual
    frame with python-can so this module stays free of a bus dependency.
    """
    if not isinstance(blt_rx, int) or isinstance(blt_rx, bool):
        raise TypeError(f"blt_rx must be an int, got {blt_rx!r}")
    if not 0 <= blt_rx <= CAN_EXT_ID_MAX:
        raise ValueError(f"blt_rx 0x{blt_rx:X} is not a valid CAN identifier")
    return blt_rx, RESET_PAYLOAD


def parse_progress(line: str) -> int | None:
    """Return 0-100 if ``line`` carries a percentage, else ``None``.

    Deliberately tolerant: any number followed by ``%`` anywhere in the line
    counts. If a line contains several (BootCommander redraws with ``\\r``, so a
    single read can carry more than one update) the LAST one wins, since that is
    the most recent state. Fractional percentages are truncated. A value outside
    0-100 is not progress and yields ``None``, so a stray "250%" in a diagnostic
    message cannot drive the progress bar.
    """
    if not line:
        return None
    matches = _PERCENT_RE.findall(line)
    if not matches:
        return None
    value = float(matches[-1])
    if not 0.0 <= value <= 100.0:
        return None
    return int(value)
