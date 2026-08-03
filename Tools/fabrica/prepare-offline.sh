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

# The openocd binary alone is useless: `-f interface/stlink.cfg` resolves
# against a search path compiled in at build time. Copying only the executable
# gives a bench that passes `doctor` and then exits 1 on the first flash.
# stlink.py adds `-s <this dir>` whenever it finds it beside the binary.
if [ -x "tools/$platform/openocd" ]; then
    echo
    echo "=== copying openocd scripts (the binary cannot find configs without them) ==="
    found_scripts=""
    for d in /usr/local/share/openocd/scripts /usr/share/openocd/scripts; do
        if [ -d "$d" ]; then found_scripts=$d; break; fi
    done
    if [ -n "$found_scripts" ]; then
        rm -rf "tools/$platform/openocd-scripts"
        cp -r "$found_scripts" "tools/$platform/openocd-scripts"
        echo "  <- $found_scripts  ($(du -sh "tools/$platform/openocd-scripts" | cut -f1))"
    else
        echo "  NOT FOUND. Flashing over SWD will fail on a machine with no"
        echo "  openocd installed. Looked in /usr/local/share/openocd/scripts"
        echo "  and /usr/share/openocd/scripts."
    fi
fi

cat <<EOF

=== done ===
This folder can now be copied to a bench of the same architecture:

    scp -r "$here" user@bench:~/

Everything the tool needs is inside it: firmware, DBCs, python packages and
native binaries. Run \`./fab doctor\` there to confirm which copies it resolved.
EOF
