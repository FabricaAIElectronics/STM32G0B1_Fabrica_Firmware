"""Tests for host detection and the dependency plan.

Every case is simulated. The point of this module is that it behaves correctly
on machines we do not have, so testing it only on the machine we do have would
miss the entire purpose.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fabrica import host  # noqa: E402


def as_host(monkeypatch, *, system="Linux", files=None, tegra=False):
    files = files or {}
    monkeypatch.setattr(host.platform, "system", lambda: system)
    monkeypatch.setattr(host, "_read", lambda p: files.get(p, ""))
    monkeypatch.setattr(host.Path, "exists",
                        lambda self: tegra and "nv_tegra_release" in str(self))


def test_jetson_detected_from_tegra_release(monkeypatch):
    as_host(monkeypatch, tegra=True)
    h = host.detect_host()
    assert h.kind == host.JETSON
    assert "mttcan" in h.can_hint


def test_jetson_detected_from_device_tree_model(monkeypatch):
    as_host(monkeypatch, files={"/proc/device-tree/model": "NVIDIA Jetson Orin NX"})
    assert host.detect_host().kind == host.JETSON


def test_raspberry_pi_detected_and_told_about_the_overlay(monkeypatch):
    as_host(monkeypatch, files={"/proc/device-tree/model": "Raspberry Pi 5 Model B"})
    h = host.detect_host()
    assert h.kind == host.RPI
    assert "dtoverlay=mcp2515" in h.can_hint
    # the oscillator trap is the classic Pi CAN fault; it must be called out
    assert "oscillator" in h.can_hint


def test_ubuntu_detected(monkeypatch):
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04 LTS"'})
    h = host.detect_host()
    assert h.kind == host.UBUNTU
    assert "22.04" in h.description


def test_jetson_wins_over_ubuntu(monkeypatch):
    """A Jetson also reports Ubuntu in os-release; the more specific kind wins."""
    as_host(monkeypatch, tegra=True,
            files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 20.04 LTS"'})
    assert host.detect_host().kind == host.JETSON


def test_pi_wins_over_ubuntu(monkeypatch):
    as_host(monkeypatch,
            files={"/proc/device-tree/model": "Raspberry Pi 4 Model B",
                   "/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04 LTS"'})
    assert host.detect_host().kind == host.RPI


def test_unknown_linux_still_gets_a_hint(monkeypatch):
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Debian 12"'})
    h = host.detect_host()
    assert h.kind == host.OTHER_LINUX
    assert "CANBusSetup" in h.can_hint


def test_non_linux_is_flagged_not_crashed(monkeypatch):
    as_host(monkeypatch, system="Windows")
    h = host.detect_host()
    assert h.kind == host.NOT_LINUX
    assert not h.is_linux
    assert any("Linux" in n for n in h.notes)


# ------------------------------------------------------------------ plan ----
def test_plan_lists_missing_apt_and_pip(monkeypatch):
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04"'})
    monkeypatch.setattr(host, "_apt_missing", lambda pkgs: ["can-utils"])
    monkeypatch.setattr(host, "_pip_missing", lambda: ["cantools"])
    monkeypatch.setattr(host.shutil, "which", lambda n: "/usr/bin/x")
    plan = host.build_plan()
    assert plan.apt == ["can-utils"]
    assert plan.pip == ["cantools"]
    assert any("apt-get install" in c and "can-utils" in c for c in plan.commands)
    assert any("pip3 install" in c and "cantools" in c for c in plan.commands)
    assert not plan.empty


def test_plan_is_empty_when_everything_is_present(monkeypatch):
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04"'})
    monkeypatch.setattr(host, "_apt_missing", lambda pkgs: [])
    monkeypatch.setattr(host, "_pip_missing", lambda: [])
    monkeypatch.setattr(host.shutil, "which", lambda n: "/usr/bin/x")
    plan = host.build_plan()
    assert plan.empty
    assert plan.commands == []


def test_missing_bootcommander_yields_a_runnable_command(monkeypatch):
    """Not a warning - a command, pointing at a script that exists.

    This used to say "run install_openblt.sh from the Linux Script/can
    directory". No such script exists in this repository, and the folder that
    text is read from does not contain the OpenBLT host sources either, so an
    operator on a fresh bench had nothing to run. `doctor` reported
    BootCommander missing and `setup` offered no way to fix it.
    """
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04"'})
    monkeypatch.setattr(host, "_apt_missing", lambda pkgs: [])
    monkeypatch.setattr(host, "_pip_missing", lambda: [])
    monkeypatch.setattr(host.shutil, "which", lambda n: None)

    plan = host.build_plan()

    assert any("install_bootcommander.sh" in c for c in plan.commands)
    assert not any("install_openblt.sh" in c for c in plan.commands)
    assert not any("install_openblt.sh" in w for w in plan.warnings)
    # The script it names must actually be there.
    script = next(c for c in plan.commands if "install_bootcommander" in c)
    assert Path(script).is_file(), f"{script} does not exist"
    assert any("STM32CubeProgrammer" in w for w in plan.warnings)


def test_a_host_needing_only_native_tools_is_not_reported_as_complete(monkeypatch):
    """apt and pip satisfied, BootCommander absent -> still work to do.

    Plan.empty keyed on apt/pip alone printed "All package dependencies are
    already installed" on exactly the bench that could not flash anything.
    """
    as_host(monkeypatch, files={"/etc/os-release": 'PRETTY_NAME="Ubuntu 22.04"'})
    monkeypatch.setattr(host, "_apt_missing", lambda pkgs: [])
    monkeypatch.setattr(host, "_pip_missing", lambda: [])
    monkeypatch.setattr(host.shutil, "which", lambda n: None)

    plan = host.build_plan()
    assert not plan.empty
    assert plan.commands


def test_apt_missing_is_empty_without_dpkg(monkeypatch):
    """On a non-dpkg system we must not claim everything is missing."""
    monkeypatch.setattr(host.shutil, "which", lambda n: None)
    assert host._apt_missing(("can-utils",)) == []
