#!/usr/bin/env sh
# Build and install BootCommander (OpenBLT's host-side flasher).
#
#   ./install_bootcommander.sh
#
# Needed because BootCommander is not apt-installable and this repo vendors only
# OpenBLT's *target* sources - the bootloader that runs on the MCU - not its
# host tools. `setup` used to point at an install_openblt.sh that does not exist
# in this repository, which is unfollowable from a deployed folder.
#
# Installs to /usr/local/bin, and also drops a copy into tools/<platform>/ so
# prepare-offline.sh can bundle it for a machine with no toolchain.
set -eu

VERSION="${1:-master}"
PREFIX="${PREFIX:-/usr/local}"
SRC_DIR="${SRC_DIR:-$HOME/.cache/fabrica/openblt-src}"
REPO="https://github.com/feaser/openblt.git"

say() { printf '\n=== %s ===\n' "$*"; }
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

here=$(cd "$(dirname "$0")" && pwd)

if command -v BootCommander >/dev/null 2>&1; then
    echo "BootCommander is already on PATH at $(command -v BootCommander)"
    echo "Re-run with FORCE=1 to rebuild anyway."
    [ "${FORCE:-0}" = "1" ] || exit 0
fi

say "installing build dependencies"
$SUDO apt-get update -qq
# libusb is for the USB transports; BootCommander over CAN does not need it,
# but LibOpenBLT will not configure without it.
$SUDO apt-get install -y --no-install-recommends \
    build-essential cmake git libusb-1.0-0-dev

say "fetching OpenBLT ($VERSION)"
mkdir -p "$(dirname "$SRC_DIR")"
if [ -d "$SRC_DIR/.git" ]; then
    git -C "$SRC_DIR" fetch --depth 1 origin "$VERSION"
    git -C "$SRC_DIR" checkout -q FETCH_HEAD
else
    git clone --depth 1 --branch "$VERSION" "$REPO" "$SRC_DIR" 2>/dev/null \
        || git clone --depth 1 "$REPO" "$SRC_DIR"
fi

# LibOpenBLT must be built first: BootCommander links against it.
say "building LibOpenBLT"
cd "$SRC_DIR/Host/Source/LibOpenBLT"
rm -rf build && mkdir build && cd build
cmake .. >/dev/null
make -j"$(nproc)"

say "building BootCommander"
cd "$SRC_DIR/Host/Source/BootCommander"
rm -rf build && mkdir build && cd build
cmake .. >/dev/null
make -j"$(nproc)"

# OpenBLT's build drops the outputs in Host/, next to the shared library.
bin=$(find "$SRC_DIR/Host" -maxdepth 2 -name BootCommander -type f | head -1)
lib=$(find "$SRC_DIR/Host" -maxdepth 2 -name "libopenblt.so*" | head -1)
[ -n "$bin" ] || { echo "build produced no BootCommander binary" >&2; exit 1; }

say "installing to $PREFIX"
$SUDO install -m 0755 "$bin" "$PREFIX/bin/BootCommander"
if [ -n "$lib" ]; then
    $SUDO install -m 0755 "$lib" "$PREFIX/lib/$(basename "$lib")"
    $SUDO ldconfig || true
fi

# Also stash it beside the tool so prepare-offline.sh can bundle it.
platform=$(python3 - <<'PY'
import platform
print(f"{platform.system().lower()}-{platform.machine().lower()}")
PY
)
# Best-effort, and never fatal: the install into $PREFIX above is what makes
# the bench work. tools/ is an optimisation for bundling, and it is often
# root-owned because someone copied the folder with sudo - dying here after a
# successful build would report failure for a machine that is now fine.
if mkdir -p "$here/tools/$platform" 2>/dev/null &&
   install -m 0755 "$bin" "$here/tools/$platform/BootCommander" 2>/dev/null; then
    [ -n "$lib" ] && install -m 0755 "$lib" \
        "$here/tools/$platform/$(basename "$lib")" 2>/dev/null
    echo "also copied into tools/$platform/ for offline bundling"
else
    echo "note: could not write tools/$platform/ (owned by another user?)."
    echo "      BootCommander is installed and working; re-run with sudo, or"
    echo "      chown the folder, if you want it bundled for offline copying."
fi

say "result"
"$PREFIX/bin/BootCommander" 2>&1 | head -3 || true
echo
echo "BootCommander installed at $PREFIX/bin/BootCommander"
echo "and copied into tools/$platform/ for offline bundling."
