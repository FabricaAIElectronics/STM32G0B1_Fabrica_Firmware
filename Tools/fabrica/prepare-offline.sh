#!/usr/bin/env sh
# Make this folder self-contained, so `scp -r` is the whole install.
#
#   ./prepare-offline.sh
#
# Fills two directories that are deliberately kept out of git:
#
#   vendor/                 python-can + cantools (~1.6 MB of pure .py)
#   tools/<platform>/       openocd + BootCommander for THIS machine
#
# Run it on a machine of the same architecture as the bench you are targeting:
# openocd is a native binary, so a copy taken on an x86 laptop is useless on a
# Jetson and vice versa. See tools/README.md.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

platform=$(python3 - <<'PY'
import platform
print(f"{platform.system().lower()}-{platform.machine().lower()}")
PY
)

echo "=== platform: $platform ==="

echo
echo "=== vendoring python dependencies into vendor/ ==="
python3 -m pip install --quiet --upgrade --target vendor -r requirements.txt
echo "vendor/ is $(du -sh vendor 2>/dev/null | cut -f1)"

echo
echo "=== copying binaries into tools/$platform/ ==="
mkdir -p "tools/$platform"
copied=0
for name in openocd BootCommander STM32_Programmer_CLI; do
    # Resolve through symlinks: BootCommander is often a link into an OpenBLT
    # checkout, and copying the link would leave a dangling pointer on the bench.
    src=$(command -v "$name" 2>/dev/null || true)
    if [ -n "$src" ]; then
        real=$(readlink -f "$src" 2>/dev/null || echo "$src")
        cp -f "$real" "tools/$platform/$name"
        echo "  $name  <- $real"
        copied=$((copied + 1))
    else
        echo "  $name  not on PATH, skipped"
    fi
done

if [ "$copied" -eq 0 ]; then
    echo
    echo "Nothing was copied. Install the tools first:"
    echo "  ./install_openocd012.sh"
fi

cat <<EOF

=== done ===
This folder can now be copied to a bench of the same architecture:

    scp -r "$here" user@bench:~/

Note: openocd still needs its scripts directory (interface/*.cfg,
target/*.cfg). Either install openocd on the bench as well, or copy
/usr/local/share/openocd/scripts across and set OPENOCD_SCRIPTS to it.
EOF
