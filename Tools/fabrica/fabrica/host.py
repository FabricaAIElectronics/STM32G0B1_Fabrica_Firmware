"""Detect the host platform and produce the right dependency plan for it.

Ubuntu, Jetson and Raspberry Pi need the same Python packages but differ in the
part that actually bites: how a CAN interface comes into existence.

  Jetson      native CAN controller (mttcan). Needs the can/mttcan modules
              loaded and, on most carrier boards, the CAN pins muxed before
              can0 exists at all.
  Raspberry Pi  usually an MCP2515 SPI HAT. Needs a dtoverlay line in
              config.txt and a reboot; the file moved to /boot/firmware/ on
              Bookworm and later.
  Generic     typically a PEAK USB adapter, which modern kernels drive with
              the in-tree peak_usb module and no setup at all.

Getting that wrong is the difference between "run this one command" and an
afternoon of guessing, so detection is worth doing properly.
"""
from __future__ import annotations

import platform
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

UBUNTU, JETSON, RPI, OTHER_LINUX, NOT_LINUX = (
    "ubuntu", "jetson", "raspberry-pi", "linux", "not-linux")

#: apt packages every Linux host needs. can-utils gives candump/cansend, which
#: are the first thing to reach for when this tool sees no traffic.
APT_PACKAGES = ("python3-pip", "python3-venv", "can-utils", "build-essential",
                "cmake", "git", "libusb-1.0-0-dev")

#: ST-Link. stlink-tools is the apt-installable fallback; STM32CubeProgrammer
#: is preferred but is a manual download from ST, so it cannot be automated.
APT_STLINK = ("stlink-tools",)

PIP_PACKAGES = ("python-can", "cantools")


@dataclass
class HostInfo:
    kind: str
    description: str
    model: str = ""
    can_hint: str = ""
    notes: list[str] = field(default_factory=list)

    @property
    def is_linux(self) -> bool:
        return self.kind != NOT_LINUX


def _read(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace").strip("\x00\n ")
    except OSError:
        return ""


def detect_host() -> HostInfo:
    if platform.system() != "Linux":
        return HostInfo(
            NOT_LINUX, f"{platform.system()} {platform.release()}",
            notes=["This tool targets Linux. Flashing and SocketCAN will not "
                   "work here; use it to review commands with --dry-run."])

    model = _read("/proc/device-tree/model") or _read("/sys/firmware/devicetree/base/model")
    os_release = _read("/etc/os-release")

    if Path("/etc/nv_tegra_release").exists() or "jetson" in model.lower():
        return HostInfo(
            JETSON, f"NVIDIA Jetson ({model or 'unknown model'})", model,
            can_hint=(
                "Jetson has a native CAN controller (mttcan). If can0 is absent:\n"
                "      sudo modprobe can can_raw mttcan\n"
                "    and mux the CAN pins for your carrier board - on most Orin\n"
                "    carriers that is busybox devmem writes, or Jetson-IO.\n"
                "    Then: sudo ./CANBusSetup.sh can0 500000"),
            notes=["Jetson ships its own Python; prefer a venv so pip does not "
                   "fight the system packages.",
                   "The CAN pins are frequently shared with other functions on "
                   "the 40-pin header - check the carrier board pinmux."])

    if "raspberry pi" in model.lower():
        cfg = ("/boot/firmware/config.txt"
               if Path("/boot/firmware/config.txt").exists() else "/boot/config.txt")
        return HostInfo(
            RPI, f"Raspberry Pi ({model})", model,
            can_hint=(
                f"A Pi normally uses an MCP2515 SPI HAT. Add to {cfg}:\n"
                "      dtparam=spi=on\n"
                "      dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25\n"
                "    then REBOOT, then: sudo ./CANBusSetup.sh can0 500000\n"
                "    Check the oscillator value against your HAT - 8 MHz and\n"
                "    16 MHz are both common, and a wrong value gives a link\n"
                "    that comes up but never receives a frame."),
            notes=["A wrong MCP2515 oscillator value is the classic Pi CAN "
                   "fault: the interface appears UP but sees no traffic."])

    if "ubuntu" in os_release.lower():
        pretty = ""
        for line in os_release.splitlines():
            if line.startswith("PRETTY_NAME="):
                pretty = line.split("=", 1)[1].strip('"')
        return HostInfo(
            UBUNTU, pretty or "Ubuntu",
            can_hint=("With a PEAK USB adapter the in-tree peak_usb driver "
                      "should create can0 on plug-in.\n"
                      "    Then: sudo ./CANBusSetup.sh can0 500000"))

    return HostInfo(OTHER_LINUX, os_release.splitlines()[0] if os_release else "Linux",
                    can_hint="Bring up SocketCAN however your adapter requires, "
                             "then: sudo ./CANBusSetup.sh can0 500000")


def _pip_missing() -> list[str]:
    missing = []
    for pkg, module in (("python-can", "can"), ("cantools", "cantools")):
        try:
            __import__(module)
        except ImportError:
            missing.append(pkg)
    return missing


def _apt_missing(packages) -> list[str]:
    """Which apt packages are not installed. Empty list if dpkg is unavailable."""
    if shutil.which("dpkg-query") is None:
        return []
    missing = []
    for pkg in packages:
        rc = subprocess.run(["dpkg-query", "-W", "-f=${Status}", pkg],
                            capture_output=True, text=True).stdout
        if "install ok installed" not in rc:
            missing.append(pkg)
    return missing


@dataclass
class Plan:
    host: HostInfo
    apt: list[str]
    pip: list[str]
    commands: list[str]
    warnings: list[str] = field(default_factory=list)

    @property
    def empty(self) -> bool:
        return not self.apt and not self.pip


def build_plan(include_stlink: bool = True) -> Plan:
    """Work out what is missing on THIS host and how to install it."""
    host = detect_host()
    if not host.is_linux:
        return Plan(host, [], _pip_missing(),
                    [], warnings=["not Linux - apt steps are not applicable"])

    wanted = list(APT_PACKAGES) + (list(APT_STLINK) if include_stlink else [])
    apt = _apt_missing(wanted)
    pip = _pip_missing()

    commands = []
    if apt:
        commands.append("sudo apt-get update")
        commands.append("sudo apt-get install -y " + " ".join(apt))
    if pip:
        commands.append("pip3 install --user " + " ".join(pip))

    warnings = []
    if shutil.which("BootCommander") is None and not Path(
            "/opt/openblt/Host/BootCommander").exists():
        warnings.append(
            "BootCommander is not installed. It is built from source, not apt: "
            "run install_openblt.sh from the Linux Script/can directory.")
    if shutil.which("STM32_Programmer_CLI") is None:
        warnings.append(
            "STM32_Programmer_CLI not found. STM32CubeProgrammer is a manual "
            "download from ST and cannot be apt-installed. stlink-tools is the "
            "fallback, but note openocd mis-parses .srec files - see README.")
    return Plan(host, apt, pip, commands, warnings)
