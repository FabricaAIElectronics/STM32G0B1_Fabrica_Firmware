# tools/ — optional local binaries

Anything here is used **in preference to `PATH`**. Populating it makes a bench
zero-install; leaving it empty changes nothing.

```
tools/
├── linux-aarch64/      openocd  BootCommander       # Jetson / Raspberry Pi 64-bit
├── linux-x86_64/       openocd  BootCommander       # ordinary Linux PC
└── windows-amd64/      openocd.exe  BootCommander.exe
```

The folder name is `<system>-<machine>`, lower-cased, exactly as Python reports
it (`platform.system()` / `platform.machine()`). Run `fabrica doctor` — it
prints the path it resolved, so you can confirm the bundled copy won.

A flat drop directly in `tools/` also works when you only care about one
machine.

## Why the binaries are not in git

`openocd` is **17 MB** and architecture-specific. The Jetson build is ARM
aarch64 and will not run on an x86 laptop, and neither will run on Windows. It
also links `libftdi1`, `libhidapi-hidraw`, `libusb-1.0` and `libudev`
dynamically, so even the correct binary needs those system libraries present.

Committing a set for every platform would add 50 MB+ to the repository, would
need rebuilding and re-committing per platform, and would still not remove the
system-library dependency. So this directory is `.gitignore`d and you fill it
for the benches you actually have.

## Filling it

On the machine itself:

```bash
./install_openocd012.sh          # builds openocd >= 0.12 into /usr/local
cp /usr/local/bin/openocd tools/$(python3 -c 'import platform;print(f"{platform.system().lower()}-{platform.machine().lower()}")')/
```

**openocd needs its scripts directory too.** The binary alone cannot find
`interface/stlink.cfg` or `target/stm32g0x.cfg`. Either leave the system
install in place, or set `OPENOCD_SCRIPTS` to a copy of
`/usr/local/share/openocd/scripts` (4.1 MB).

## Why ≥ 0.12 matters

Ubuntu 22.04 ships openocd 0.11, whose flash driver has no entry for device id
`0x467`. On an STM32G0B1 it attaches, reads memory and identifies the core, then
fails at `auto_probe` — **after halting the target**, so the board goes silent
and looks dead. `doctor` predicts this, and `flash` refuses rather than trying.
Pinning a known-good build here is the cleanest way to avoid inheriting the
distro one from `PATH`.
