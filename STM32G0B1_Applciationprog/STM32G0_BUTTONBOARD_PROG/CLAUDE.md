# ButtonBoard V5.5 firmware — project rules

STM32G0B1RET6, bare-metal, STM32CubeIDE build, OpenBLT user program linked at
0x08003000. General embedded coding standards: see
`../KincoDrive_ControlModule_V5_4/CLAUDE.md`. This file covers what is
specific to this project.

## The `.ioc` is documentation, not a code generator

`STM32G0_BUTTONBOARD_PROG.ioc` is kept as the pinout/peripheral record and
for CubeMX's pin database (it is the authority on per-pin AF numbers, e.g.
I2C3 on PA6/PA7 is **AF9**, not AF6). **The C code diverged from CubeMX
long ago and is hand-written.** These files are NOT CubeMX-managed and carry
no `USER CODE` fences, so a CubeMX *Generate Code* **overwrites them**:

- `Core/Src/main.c` — the whole application entry (`VectorBase_Config`,
  `Leds_Init`, `CAN_Init`, `I2CHost_Init`, `AppLogic_Init/Run`, heartbeat)
- `Core/Src/stm32g0xx_hal_msp.c` — pin/AF/NVIC wiring with board rationale
- `Core/Src/stm32g0xx_it.c` — FDCAN + I2C1 handlers
- `Core/Inc/main.h`, `Core/Inc/stm32g0xx_hal_conf.h`

A regen also silently **upgrades the vendored HAL** under `Drivers/`
(`LibraryCopy=1`) and rewrites `.cproject`/`.mxproject`. Every review and
HAL trace for this project was done against **FW_G0 1.6.2 (HAL 1.4.6)**.

### Rules
1. **Do not press Generate Code.** Change the `.ioc` in CubeMX freely (pin
   labels, pull-ups, timings) and port the change to the C by hand, reading
   it out of the `.ioc` diff.
2. The `.ioc` is set to `NoMain=true` (never emits `main.c`),
   `LastFirmware=false` + `FirmwarePackage=FW_G0 V1.6.2` (never auto-migrates
   the HAL), `BackupPrevious=true` (leaves `.bak` files if a regen happens
   anyway). Do not undo these.
3. **If a regen has already happened:** do not build or flash it. Restore
   with `git checkout HEAD -- Core/Src/main.c Core/Src/stm32g0xx_hal_msp.c
   Core/Src/stm32g0xx_it.c Core/Inc/main.h Core/Inc/stm32g0xx_hal_conf.h
   Drivers .cproject .mxproject`, keep only the intended `.ioc` diff (drop the
   `MxCube.Version` / `FirmwarePackage` stamp lines), then re-apply any
   uncommitted bench deltas by hand.
4. Bench-only deltas (HSI clock + CSS off in `main.c`, encoder `GPIO_PULLUP`
   in `stm32g0xx_hal_msp.c`) are marked `Bench only` in comments and must
   stay **uncommitted** — stage those files by hunk.

### These guards are the interim, not the end state
The intended fix is the standard ST workflow: move every hand-written block
into the CubeMX `USER CODE BEGIN/END` fences so a regen is safe **by
construction** and the `.ioc` becomes a live generator again (then set
`NoMain=false` back and retarget the tripwire test to "no diff outside
fences"). It was deliberately NOT done on 2026-08-15 because the divergence
is deep (bootloader VTOR hand-off before `HAL_Init`, `Leds_Init` ordered
before the peripheral inits, `CAN_Init` returning `bool`, internal pull-ups
the generated MSP omits) and re-laying out verified files means re-running
the whole bench sequence. Do it as its own task, after the branch merges or on
a sub-branch — see the "Migrate ButtonBoard C into CubeMX USER CODE fences"
task if it is still queued.

## Two buses, one slave
- I2C1 (PB6/PB7, AF6, connector P1): STM32 is a **pure slave at 0x51**
  speaking the V5.2 ATtiny protocol (`i2c_host*.c`, `i2c_host_proto*.[ch]`
  — the proto layer is HAL-free and host-tested in `vv/unit`).
- I2C3 (PA6/PA7, **AF9**, 100 kHz): AT24C256 config EEPROM, STM32 is the
  only master (`eeprom_driver.c`, `hi2c3`).

## Verification
Host tests: `make -C vv/unit clean all` (compiles the real
`i2c_host_proto.c` and `encoder_math.h`). Firmware: headless CubeIDE build
must be 0 errors / 0 warnings. Bench: `Docs/superpowers/plans/2026-08-14-
i2c-host-compat.md` results section records what has been proven on
hardware and how.
