#!/usr/bin/env bash
# Build and install openocd >= 0.12 alongside the distro package.
#
# Why this exists: Ubuntu 22.04 (jammy/universe) ships openocd 0.11.0 and has no
# backport. 0.11's stm32l4x flash driver - which is what covers the STM32G0
# family - has no entry for device id 0x467, so on an STM32G0B1 it attaches,
# reads memory and identifies the core correctly, then fails at `auto_probe`
# when asked to write. Support for 0x467 landed in 0.12.0.
#
# That blocks SWD bootloader flashing on every G0B1 board in this fleet
# (KincoDrive, PowerStage, LEDDriver). The F303 is unaffected - it goes through
# stm32f1x, supported long before 0.11 - and flashing applications over CAN
# never involves openocd at all.
#
# Installs to /usr/local so the distro package stays in place and can be fallen
# back to; /usr/local/bin precedes /usr/bin on a default PATH.
#
# Usage:  ./install_openocd012.sh [version]     (default 0.12.0)
set -euo pipefail

VERSION="${1:-0.12.0}"
PREFIX="${PREFIX:-/usr/local}"
SRC_DIR="${SRC_DIR:-$HOME/.cache/fabrica/openocd-src}"
REPO="https://github.com/openocd-org/openocd.git"   # the project's own mirror

say() { printf '\n=== %s ===\n' "$*"; }

if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

say "checking for an already-good openocd"
for candidate in "$PREFIX/bin/openocd" "$(command -v openocd || true)"; do
    [ -x "$candidate" ] || continue
    have="$("$candidate" --version 2>&1 | sed -n 's/.*Open On-Chip Debugger \([0-9]*\.[0-9]*\).*/\1/p' | head -1)"
    [ -n "$have" ] || continue
    major="${have%%.*}"; minor="${have##*.}"
    if [ "$major" -gt 0 ] || [ "$minor" -ge 12 ]; then
        echo "$candidate is already $have - nothing to do"
        exit 0
    fi
    echo "$candidate is $have (too old)"
done

say "installing build dependencies"
$SUDO apt-get update -qq
# libusb/libhidapi/libftdi cover the ST-Link, CMSIS-DAP and FTDI adapters;
# pkg-config and the autotools chain are needed by ./bootstrap.
$SUDO apt-get install -y --no-install-recommends \
    build-essential git autoconf automake libtool pkg-config texinfo \
    libusb-1.0-0-dev libhidapi-dev libftdi1-dev libjaylink-dev

say "fetching openocd v$VERSION"
mkdir -p "$(dirname "$SRC_DIR")"
if [ -d "$SRC_DIR/.git" ]; then
    git -C "$SRC_DIR" fetch --depth 1 origin "tag" "v$VERSION"
    git -C "$SRC_DIR" checkout -q "v$VERSION"
    git -C "$SRC_DIR" submodule update --init --recursive --depth 1
else
    # Submodules (jimtcl, libjaylink) are required; a plain clone will not
    # configure.
    git clone --depth 1 --branch "v$VERSION" --recurse-submodules \
        --shallow-submodules "$REPO" "$SRC_DIR"
fi

say "configuring"
cd "$SRC_DIR"
./bootstrap
# --enable-stlink is the only adapter this bench strictly needs, but the others
# cost little and make the binary reusable on other benches.
./configure --prefix="$PREFIX" --enable-stlink --enable-cmsis-dap --enable-ftdi

say "building"
make -j"$(nproc)"

say "installing to $PREFIX"
$SUDO make install

say "result"
hash -r 2>/dev/null || true
"$PREFIX/bin/openocd" --version 2>&1 | head -2
echo
echo "openocd $VERSION installed at $PREFIX/bin/openocd"
echo "The distro package is untouched at /usr/bin/openocd."
