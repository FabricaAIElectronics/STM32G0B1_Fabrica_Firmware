# ButtonBoard V5.5 firmware — project rules

STM32G0B1RET6, bare-metal, STM32CubeIDE build, OpenBLT user program linked at
0x08003000. General embedded coding standards: see
`../KincoDrive_ControlModule_V5_4/CLAUDE.md`. This file covers what is
specific to this project.

## The `.ioc` is a LIVE code generator - hand-written code lives in USER CODE fences

Since 2026-08-19 `main.c`, `stm32g0xx_hal_msp.c`, `stm32g0xx_it.c`, `main.h`
and `stm32g0xx_hal_conf.h` are **CubeMX-managed**: everything outside a
`/* USER CODE BEGIN x */ ... /* USER CODE END x */` fence is generated from
`STM32G0_BUTTONBOARD_PROG.ioc` and is rewritten by *Generate Code*; every
hand-written block sits inside a fence. A real regen was run against the
fenced tree and changed **zero lines** outside fences (proof in
`vv/tests/test_buttonboard_handwritten.py`'s history / the 2026-08-19 commit).

What lives where:
- `USER CODE 1` (before `HAL_Init`): `VectorBase_Config()` - the OpenBLT VTOR
  hand-off must precede the SysTick enable.
- `MX_GPIO_Init_2`: `Leds_Init()` - parks the LED buffer nets before any other
  peripheral init (earlier than the old hand-written order; strictly safer).
- `USER CODE 2`: `I2CHost_Init()`, `CAN_Init()` -> `AppLogic_Init(&sm, can_ok)`.
- `USER CODE 3` (inside `while(1)`, before its closing brace): the heartbeat
  and `AppLogic_Run()`. NOT the gap between `END WHILE` and `BEGIN 3` - that
  is generated territory and a regen deletes it (this exact mistake was made
  and caught during the migration).
- `*_MspInit 0` fences: board rationale comments only - the pin/AF/pull/NVIC
  DECISIONS are generated from the .ioc (I2C1 pull-ups, CAN RX pull-up, I2C3
  **AF9**, I2C1_IRQn prio 1, UCPD dead-battery strobe, I2C1 OwnAddress 0x51).
- `main.h` `Includes`/`ET` fences: `board_pins.h` + the shared `extern`
  handles. Generated `main.h` emits all 88 `*_Pin`/`*_GPIO_Port` defines from
  the .ioc labels; `board_pins.h` keeps identical, `#ifndef`-guarded copies as
  the annotated explanation. Edit the .ioc label first, then the comment.

### Rules
1. **Generate Code is now safe** - but change peripheral/pin settings in the
   `.ioc`, never by editing generated lines (they revert on regen).
2. New hand-written code goes in a fence. `vv/tests/test_buttonboard_handwritten.py`
   fails the gate if any listed marker is missing or sits outside a fence.
3. The `.ioc` stays pinned: `FirmwarePackage=FW_G0 V1.6.2` (vendored HAL
   1.4.6), `LastFirmware=false`, `KeepUserCode=true`, `NoMain=false`,
   `BackupPrevious=true`. Do not undo these; the tripwire checks them.
4. Bench-only deltas: the encoder `GPIO_PULLUP` lives in the `TIM2_MspInit 1`
   fence (survives regen, still uncommitted); the Nucleo HSI clock delta is a
   direct edit of the generated `SystemClock_Config` and **is reverted by any
   regen - re-apply it after regenerating** (it is marked "Bench only" and
   must stay uncommitted either way).

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
