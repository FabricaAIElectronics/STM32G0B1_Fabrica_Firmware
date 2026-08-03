# OpenBLT — vendored bootloader core

This is a **partial, verbatim copy** of the OpenBLT target sources, vendored so that the
four bootloader projects in this repo build from a clean checkout with no external clone
and no machine-specific paths.

| | |
|---|---|
| Upstream | https://github.com/feaser/openblt |
| Commit | `e34dd696dd4c082c6168dc343ca322a7a35fc155` (2026-07-13) |
| Core version | 1.22.0 (`BOOT_VERSION_CORE_*` in `Target/Source/boot.h`) |
| License | GPLv3 — see `Doc/license.html`. A commercial license is available from Feaser. |

## What is included

```
Target/Source/*.c *.h *.dox      loader core (boot, com, xcp, can, cop, file, net, mb, …)
Target/Source/ARMCM0_STM32G0/    port for the three STM32G0B1 bootloaders
Target/Source/ARMCM4_STM32F3/    port for the STM32F303RE bootloader
```

## What is deliberately omitted

- `Target/Source/third_party/` (12 MB — uIP, FatFS, USB stacks). Every include of it is
  behind `#if (BOOT_COM_NET_ENABLE > 0)` / `#if (BOOT_FILE_SYS_ENABLE > 0)`, and both are
  `0` in all four `blt_conf.h` files. **If you ever enable networking, a file system, or
  USB, you must vendor `third_party/` as well.**
- All other MCU ports (`ARMCM4_STM32F4`, `HCS12`, …).
- `Host/` (BootCommander and friends — a host-side tool, not built here) and the rest of
  `Doc/`.

## Do not edit these files

Local changes here are silently lost on the next update. All per-device configuration
belongs in each project's own `App/blt_conf.h`, `App/flash_layout.c`, and `App/hooks.c`.

## How to update

1. `git clone https://github.com/feaser/openblt` (or pull an existing clone).
2. Re-copy the three paths listed under "What is included" over this directory.
3. Update the commit hash and version in the table above.
4. Rebuild all four bootloaders and re-verify a flash over CAN on each board.

## How the projects reference it

Each bootloader's `.project` links this directory as a folder named `Loader` via
`PARENT-2-PROJECT_LOC`, and `.cproject` adds three include paths under `${ProjDirPath}`.
Both are repo-relative — nothing points outside the checkout. A resource filter in
`.project` limits the linked folder to the one port that project needs.
