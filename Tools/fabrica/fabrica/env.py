"""Environment discovery and the `doctor` pre-flight check.

This tool was written without access to the bench, so the first thing it must do
on a real target is prove its own assumptions rather than discover them halfway
through flashing a board. `fabrica doctor` answers, in order:

  can I see the tools, can I see the CAN interface, is it up at the right
  bitrate, do I have permission, and do the firmware images match the manifest

Every check returns a Check with a remedy string, so a failure tells you what to
run next instead of just what went wrong.
"""
from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

# Preference order chosen with Jordan: ST's own CLI first (handles .srec
# directly, clearest diagnostics), then openocd, then stlink-tools.
STLINK_BACKENDS = ("STM32_Programmer_CLI", "openocd", "st-flash")

def _home_candidate() -> str | None:
    """~/openblt/Host/BootCommander, if a home directory can be resolved.

    Path.home() raises RuntimeError when neither HOME nor USERPROFILE is set -
    service accounts, containers, some cron environments. Calling it at import
    time made the whole module unimportable there, taking the CLI with it.
    """
    try:
        return str(Path.home() / "openblt" / "Host" / "BootCommander")
    except RuntimeError:
        return None


BOOTCOMMANDER_CANDIDATES = tuple(c for c in (
    "BootCommander",                                  # on PATH via /usr/local/bin
    "/opt/openblt/Host/BootCommander",
    _home_candidate(),
) if c)

OK, WARN, FAIL = "ok", "warn", "fail"


@dataclass
class Check:
    name: str
    status: str
    detail: str
    remedy: str = ""


@dataclass
class Environment:
    stlink_backend: str | None = None
    stlink_path: str | None = None
    bootcommander: str | None = None
    can_interfaces: list[str] = field(default_factory=list)
    checks: list[Check] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return all(c.status != FAIL for c in self.checks)


def _run(cmd: list[str], timeout: int = 10) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except (OSError, subprocess.SubprocessError) as exc:
        return 1, str(exc)


def find_stlink() -> tuple[str | None, str | None]:
    """Return (backend name, path) for the first available ST-Link backend."""
    for backend in STLINK_BACKENDS:
        path = shutil.which(backend)
        if path:
            return backend, path
    return None, None


def find_bootcommander() -> str | None:
    for candidate in BOOTCOMMANDER_CANDIDATES:
        path = shutil.which(candidate) if os.sep not in candidate else candidate
        if path and Path(path).is_file():
            return path
    return None


def can_interfaces() -> list[str]:
    """SocketCAN interfaces currently known to the kernel."""
    sysclass = Path("/sys/class/net")
    if not sysclass.is_dir():
        return []
    found = []
    for iface in sorted(sysclass.iterdir()):
        if (iface / "can_bittiming").exists() or iface.name.startswith(("can", "vcan")):
            found.append(iface.name)
    return found


def can_link_state(iface: str) -> dict:
    """Parse `ip -details link show <iface>` into the fields we care about."""
    rc, out = _run(["ip", "-details", "link", "show", iface])
    if rc != 0:
        return {"present": False}
    info = {"present": True, "up": " state UP " in out or "<NO-CARRIER,NOARP,UP" in out
            or ",UP," in out.split("\n")[0]}
    for token, key in (("bitrate ", "bitrate"), ("can state ", "can_state")):
        if token in out:
            info[key] = out.split(token, 1)[1].split()[0]
    return info


def _openocd_version_checks(path: str) -> list[Check]:
    """Flag MCU families this openocd build is too old to program.

    Worth a dedicated check because the failure it predicts is expensive to
    diagnose live: openocd 0.11 (the newest in Ubuntu 22.04) connects to a
    G0B1, reads its memory, reports the right core -- and only then refuses to
    flash, having already halted the target. Everything looks healthy until the
    one operation that matters. See OPENOCD_MIN_VERSION in stlink.py.
    """
    from . import stlink  # local: stlink imports from this module

    rc, out = _run([path, "--version"])
    version = stlink.parse_openocd_version(out)
    if version is None:
        return [Check("openocd version", WARN,
                      "could not parse `openocd --version`",
                      "check manually that it supports your MCU")]

    have = ".".join(str(n) for n in version)
    blocked: list[str] = []
    for family, minimum in stlink.OPENOCD_MIN_VERSION.items():
        if version < minimum:
            need = ".".join(str(n) for n in minimum)
            blocked.append(f"{family} (needs >= {need})")

    if not blocked:
        return [Check("openocd version", OK, have)]

    # Not a hard FAIL: openocd is still the right backend for any other board
    # on the bench, and flashing over CAN does not involve it at all.
    return [Check(
        "openocd version", WARN,
        f"openocd {have} cannot SWD-flash: {', '.join(blocked)}",
        "install STM32CubeProgrammer, or build openocd >= 0.12. "
        "Flashing the application over CAN is unaffected; only writing the "
        "bootloader through SWD needs this")]


def doctor(firmware_dir: Path | str | None = None, iface: str = "can0",
           bitrate: int = 500000) -> Environment:
    """Run every pre-flight check. Never raises; every problem becomes a Check."""
    env = Environment()

    # --- ST-Link ---------------------------------------------------------
    backend, path = find_stlink()
    env.stlink_backend, env.stlink_path = backend, path
    if backend == "st-flash":
        # Every image we ship is a .srec, and st-flash has no S-record parser.
        # Reporting OK here would give a green doctor followed by a failure on
        # the very first flash, which is the worst possible order to find out.
        env.checks.append(Check(
            "stlink", FAIL,
            f"only st-flash found ({path}); it cannot program the .srec images "
            f"this project builds",
            "install STM32CubeProgrammer (STM32_Programmer_CLI) - preferred - "
            "or openocd. st-flash would need every image converted to .bin "
            "first with arm-none-eabi-objcopy"))
    elif backend == "openocd":
        env.checks.append(Check("stlink", OK, f"{backend} at {path}"))
        env.checks.extend(_openocd_version_checks(path))
    elif backend:
        env.checks.append(Check("stlink", OK, f"{backend} at {path}"))
    else:
        env.checks.append(Check(
            "stlink", FAIL, "no ST-Link backend found",
            "install one of: STM32CubeProgrammer (STM32_Programmer_CLI), "
            "openocd, or `sudo apt install stlink-tools`"))

    # --- BootCommander ---------------------------------------------------
    env.bootcommander = find_bootcommander()
    if env.bootcommander:
        env.checks.append(Check("bootcommander", OK, env.bootcommander))
    else:
        env.checks.append(Check(
            "bootcommander", FAIL, "BootCommander not found",
            "run install_openblt.sh, or put BootCommander on PATH"))

    # --- python-can / cantools ------------------------------------------
    for mod, remedy in (("can", "pip install python-can"),
                        ("cantools", "pip install cantools")):
        try:
            __import__(mod)
            env.checks.append(Check(mod, OK, "importable"))
        except ImportError:
            env.checks.append(Check(mod, FAIL, f"{mod} not installed", remedy))

    # --- CAN interface ---------------------------------------------------
    env.can_interfaces = can_interfaces()
    if not env.can_interfaces:
        env.checks.append(Check(
            "can-iface", FAIL, "no SocketCAN interfaces found",
            "check wiring/driver; on Jetson/RPi confirm the CAN overlay is loaded"))
    elif iface not in env.can_interfaces:
        env.checks.append(Check(
            "can-iface", FAIL,
            f"{iface} not present (have: {', '.join(env.can_interfaces)})",
            f"use --iface <name>, or bring up {iface}"))
    else:
        state = can_link_state(iface)
        actual = state.get("bitrate")
        if not state.get("up"):
            env.checks.append(Check(
                "can-link", FAIL, f"{iface} is DOWN",
                f"sudo ./CANBusSetup.sh {iface} {bitrate}"))
        elif actual and int(actual) != bitrate:
            env.checks.append(Check(
                "can-link", FAIL,
                f"{iface} is up at {actual} bps, expected {bitrate}",
                f"sudo ./CANBusSetup.sh {iface} {bitrate}"))
        else:
            env.checks.append(Check(
                "can-link", OK,
                f"{iface} up" + (f" at {actual} bps" if actual else "")))
        if state.get("can_state") in ("BUS-OFF", "ERROR-PASSIVE"):
            env.checks.append(Check(
                "can-health", WARN, f"{iface} is {state['can_state']}",
                "check termination and that another node is present"))

    # --- firmware manifest ----------------------------------------------
    from .manifest import ManifestError, load_manifest, verify_all
    try:
        # Accept a loose .srec drop as well as a staged set, exactly as the
        # flash and list commands do. Without this, doctor reported FAIL on a
        # perfectly usable folder of images just because it had no manifest.
        from . import sources
        found = sources.discover(firmware_dir or "", max_depth=0)
        man = sources.load(found[0]) if found else load_manifest(firmware_dir)
        problems = verify_all(man)
        if problems:
            env.checks.append(Check(
                "firmware", FAIL, f"{len(problems)} image problem(s): {problems[0]}",
                "re-stage with: python vv/run_gate.py --stage-artifacts"))
        else:
            n = len(man.boards)
            env.checks.append(Check(
                "firmware", OK,
                f"{n} boards, all checksums match (git {man.git_sha[:8]})"))
        if man.git_dirty:
            env.checks.append(Check(
                "firmware-provenance", WARN,
                "images were staged from a DIRTY working tree",
                "commit, re-run the gate, and re-stage before a release build"))
        if man.gate != "pass":
            env.checks.append(Check(
                "firmware-provenance", WARN,
                f"manifest records gate={man.gate!r}", "re-run the V&V gate"))
    except ManifestError as exc:
        env.checks.append(Check("firmware", FAIL, str(exc).splitlines()[0],
                                "python vv/run_gate.py --stage-artifacts"))

    return env
