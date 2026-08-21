# V5.2-Compatible I2C Host Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ButtonBoard V5.5 firmware answer the V5.2 ATtiny's I2C slave protocol at 7-bit address 0x51, so existing Jetson host code works unchanged.

**Architecture:** A pure protocol module (`i2c_host_proto.c`, no HAL includes, host-testable exactly like `battery.c`) builds every reply from a plain snapshot struct. A thin HAL glue module (`i2c_host.c`) runs I2C1 (PB6/PB7, connector P1) as an interrupt-driven pure slave for the host. The AT24C256 EEPROM moves to its own bus, I2C3 (PA6/PA7, 100 kHz) — the buses are fully independent, so no dual-role or arbitration logic exists. EEPROM driver return codes stop being discarded as part of the migration. *(Revised 2026-08-15 from a shared-bus design after the PCB routed the EEPROM to I2C3.)*

**Tech Stack:** STM32G0 HAL (vendored), vv host unit harness (`vv/unit/harness.h`, gcc), STM32CubeIDE headless build.

**Spec:** `Docs/superpowers/specs/2026-08-14-i2c-host-compat-design.md` — byte-format table lives there; this plan repeats every format in code.

## Global Constraints

- Slave address: 7-bit **0x51** (`0x51 << 1` in HAL `OwnAddress1`).
- Byte orders are the ATtiny's, verbatim: analog and encoder **reads big-endian**, encoder **write little-endian**.
- READ_ALL reply is exactly 7 bytes: knob BE16, ref BE16, encoder BE16, button.
- Reference voltage is fixed 3300 mV. Rotary mV = `rotary_pos * 550`; on `rotary_pos == 0xFF` hold the last valid value.
- Version reply: major 2, minor 0. UID reply: first **10** bytes of the STM32 96-bit UID.
- `i2c_host_proto.[ch]` may include only `<stdint.h>`/`<stdbool.h>` — it must compile on the host with no fakes.
- The working tree carries three uncommitted **bench-only deltas** (HSI clock + CSS off in `main.c`, encoder pull-ups in `stm32g0xx_hal_msp.c`). Do not commit them; `git add` specific hunks/files only.
- Repo commit style: imperative sentence, no `feat:` prefix. End commit messages with the Claude Code co-author trailer.

## File Structure

- Create `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host_proto.h` — command IDs, snapshot struct, pure reply builder (single responsibility: wire format).
- Create `.../Core/Src/i2c_host_proto.c` — implementation of the above.
- Create `.../Core/Inc/i2c_host.h` + `.../Core/Src/i2c_host.c` — HAL slave glue for I2C1: listen management, callbacks, snapshot publish.
- Create `vv/unit/test_i2c_host_proto.c` — host tests compiling the real `i2c_host_proto.c`.
- Modify `vv/unit/Makefile` — new test target (battery pattern).
- Modify `.../Core/Src/main.c` (I2C1 OwnAddress1 + host init; new `hi2c3` + `MX_I2C3_Init`), `.../Core/Inc/main.h` (`extern hi2c3`), `.../Core/Src/stm32g0xx_hal_msp.c` (I2C3 MSP branch), `.../Core/Inc/board_pins.h` (HOST_/EEPROM_ pin names), `.../Core/Src/stm32g0xx_it.c` (I2C1 IRQ), `.../Core/Src/applogic.c` (publish + EEPROM error), `.../Core/Src/eeprom_driver.c` + `Inc/eeprom_driver.h` (`hi2c3`, status returns, ACK-poll fix).
- Already done in the `.ioc` (uncommitted, by Jordan): I2C3 on PA6/PA7 at `Timing 0x10A077A8`, PA0 re-bound to `TIM2_CH1`, and `GPIO_Label`s for all 44 function pins matching `board_pins.h`.

---

### Task 1: Pure protocol module + host tests

**Files:**
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host_proto.h`
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/i2c_host_proto.c`
- Create: `vv/unit/test_i2c_host_proto.c`
- Modify: `vv/unit/Makefile` (TESTS line 6, new explicit rule after the `test_battery_soc` rule ~line 36)

**Interfaces:**
- Produces: `I2CHostProtoState` struct; `uint8_t I2CHostProto_BuildReply(uint8_t cmd, const I2CHostProtoState *st, uint8_t out[I2CHOST_MAX_REPLY])` returning reply length; `uint16_t I2CHostProto_RotaryMv(uint8_t rotary_pos, uint16_t last_mv)`; `int16_t I2CHostProto_ParseEncoderWrite(const uint8_t d[2])`; command macros `I2CHOST_CMD_*`. Task 2 consumes all of these.

- [ ] **Step 1: Write the failing test** — `vv/unit/test_i2c_host_proto.c`:

```c
/* Host-side assertions for the V5.2-compatible I2C reply formats.
 * Compiles the real Core/Src/i2c_host_proto.c, battery.c-style. */
#include "harness.h"
#include "i2c_host_proto.h"

int main(void)
{
    uint8_t out[I2CHOST_MAX_REPLY];
    I2CHostProtoState st = {
        .knob_mv = 1650, .ref_mv = 3300, .encoder = -104, .button = 1,
        .uid = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04,0x05,0x06},
    };

    /* READ_ALL: 7 bytes, analog + encoder big-endian, button last. */
    VV_EQ_U32("read_all_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_ALL, &st, out), 7);
    VV_EQ_U32("read_all_knob_be", (out[0] << 8) | out[1], 1650);
    VV_EQ_U32("read_all_ref_be",  (out[2] << 8) | out[3], 3300);
    VV_EQ_U32("read_all_enc_be",  (int16_t)((out[4] << 8) | out[5]), (unsigned long)(int16_t)-104);
    VV_EQ_U32("read_all_button",  out[6], 1);

    /* Encoder read is BE; encoder write is LE - the ATtiny asymmetry. */
    VV_EQ_U32("enc_read_len", I2CHostProto_BuildReply(I2CHOST_CMD_RW_ENCODER, &st, out), 2);
    VV_EQ_U32("enc_read_be", (int16_t)((out[0] << 8) | out[1]), (unsigned long)(int16_t)-104);
    { uint8_t wr[2] = {0x9C, 0xFF};   /* -100 little-endian */
      VV_EQ_U32("enc_write_le", (unsigned long)(int16_t)I2CHostProto_ParseEncoderWrite(wr),
                (unsigned long)(int16_t)-100); }

    VV_EQ_U32("button_len", I2CHostProto_BuildReply(I2CHOST_CMD_RW_ENCODER_BUTTON, &st, out), 1);
    VV_EQ_U32("button_val", out[0], 1);

    VV_EQ_U32("version_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_VERSION, &st, out), 2);
    VV_EQ_U32("version_major", out[0], 2);
    VV_EQ_U32("version_minor", out[1], 0);

    VV_EQ_U32("uid_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_UID, &st, out), 10);
    VV_EQ_U32("uid_first", out[0], 0xDE);
    VV_EQ_U32("uid_last",  out[9], 0x06);

    /* Unknown command: full buffer of 0xFF so the slave never clock-starves. */
    VV_EQ_U32("unknown_len", I2CHostProto_BuildReply(0x42, &st, out), I2CHOST_MAX_REPLY);
    VV_EQ_U32("unknown_fill", out[0], 0xFF);

    /* Rotary ladder synthesis: pos * 550, hold-last on invalid (0xFF). */
    VV_EQ_U32("rotary_pos0", I2CHostProto_RotaryMv(0, 1234), 0);
    VV_EQ_U32("rotary_pos6", I2CHostProto_RotaryMv(6, 1234), 3300);
    VV_EQ_U32("rotary_hold", I2CHostProto_RotaryMv(0xFF, 1234), 1234);

    VV_REPORT();
}
```

- [ ] **Step 2: Add the Makefile target and run to verify it fails.** In `vv/unit/Makefile` change line 6 to
  `TESTS   := test_can_layout test_thermistor_math test_battery_soc test_i2c_host_proto`
  and add after the `test_battery_soc` rule (same pattern — real module compiled in):

```make
BB_CORE := ../../STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core
$(BINDIR)/test_i2c_host_proto: test_i2c_host_proto.c $(BB_CORE)/Src/i2c_host_proto.c \
                               $(BB_CORE)/Inc/i2c_host_proto.h $(HEADERS) | $(BINDIR)
	$(CC) $(CFLAGS) -I$(BB_CORE)/Inc -o $@ \
	    test_i2c_host_proto.c $(BB_CORE)/Src/i2c_host_proto.c -lm
```

Run: `make -C vv/unit clean all`
Expected: FAIL to compile — `i2c_host_proto.h: No such file or directory`.

- [ ] **Step 3: Write the module.** `Core/Inc/i2c_host_proto.h`:

```c
/* V5.2 ATtiny-compatible I2C host protocol - pure wire-format layer.
 * No HAL includes: this file also compiles on the host for vv/unit. */
#ifndef INC_I2C_HOST_PROTO_H_
#define INC_I2C_HOST_PROTO_H_

#include <stdint.h>

#define I2CHOST_ADDR_7BIT        0x51U  /* V5.2 strap wiring: 0x49 + 8      */

#define I2CHOST_CMD_READ_KNOB_1  0x05U
#define I2CHOST_CMD_READ_REF_V   0x08U
#define I2CHOST_CMD_READ_ALL     0x09U  /* power-on default, as on the ATtiny */
#define I2CHOST_CMD_READ_UID     0x10U
#define I2CHOST_CMD_RW_ENCODER   0x80U
#define I2CHOST_CMD_RW_ENCODER_BUTTON 0x84U
#define I2CHOST_CMD_READ_VERSION 0xFEU

#define I2CHOST_VERSION_MAJOR    2U     /* ATtiny reported 1,0 */
#define I2CHOST_VERSION_MINOR    0U

#define I2CHOST_UID_LEN          10U    /* ATtiny reply length; STM32 UID truncated */
#define I2CHOST_MAX_REPLY        16U
#define I2CHOST_REF_MV           3300U
#define I2CHOST_ROTARY_STEP_MV   550U   /* 6 x 10k ladder, COM at 3.3 V */

typedef struct {
    uint16_t knob_mv;                   /* synthesized rotary ladder voltage */
    uint16_t ref_mv;
    int16_t  encoder;
    uint8_t  button;
    uint8_t  uid[I2CHOST_UID_LEN];
} I2CHostProtoState;

/* Fill out[] with the reply for cmd; returns the reply length. Unknown
 * commands fill the whole buffer with 0xFF and return I2CHOST_MAX_REPLY. */
uint8_t I2CHostProto_BuildReply(uint8_t cmd, const I2CHostProtoState *st,
                                uint8_t out[I2CHOST_MAX_REPLY]);

/* rotary_pos 0..6 -> pos * 550 mV; anything else holds last_mv. */
uint16_t I2CHostProto_RotaryMv(uint8_t rotary_pos, uint16_t last_mv);

/* Encoder write payload is little-endian - the ATtiny asymmetry. */
int16_t I2CHostProto_ParseEncoderWrite(const uint8_t d[2]);

#endif /* INC_I2C_HOST_PROTO_H_ */
```

`Core/Src/i2c_host_proto.c`:

```c
#include "i2c_host_proto.h"

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

uint8_t I2CHostProto_BuildReply(uint8_t cmd, const I2CHostProtoState *st,
                                uint8_t out[I2CHOST_MAX_REPLY])
{
    switch (cmd) {
    case I2CHOST_CMD_READ_KNOB_1:
        put_be16(out, st->knob_mv);
        return 2U;
    case I2CHOST_CMD_READ_REF_V:
        put_be16(out, st->ref_mv);
        return 2U;
    case I2CHOST_CMD_READ_ALL:
        put_be16(&out[0], st->knob_mv);
        put_be16(&out[2], st->ref_mv);
        put_be16(&out[4], (uint16_t)st->encoder);
        out[6] = st->button;
        return 7U;
    case I2CHOST_CMD_RW_ENCODER:
        put_be16(out, (uint16_t)st->encoder);
        return 2U;
    case I2CHOST_CMD_RW_ENCODER_BUTTON:
        out[0] = st->button;
        return 1U;
    case I2CHOST_CMD_READ_VERSION:
        out[0] = I2CHOST_VERSION_MAJOR;
        out[1] = I2CHOST_VERSION_MINOR;
        return 2U;
    case I2CHOST_CMD_READ_UID:
        for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) out[i] = st->uid[i];
        return I2CHOST_UID_LEN;
    default:
        for (uint8_t i = 0U; i < I2CHOST_MAX_REPLY; i++) out[i] = 0xFFU;
        return I2CHOST_MAX_REPLY;
    }
}

uint16_t I2CHostProto_RotaryMv(uint8_t rotary_pos, uint16_t last_mv)
{
    if (rotary_pos <= 6U) {
        return (uint16_t)(rotary_pos * I2CHOST_ROTARY_STEP_MV);
    }
    return last_mv;
}

int16_t I2CHostProto_ParseEncoderWrite(const uint8_t d[2])
{
    return (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
}
```

- [ ] **Step 4: Run tests, verify pass.** Run: `make -C vv/unit clean all`
Expected: `TEST PASS` lines for every assertion, `... 0 failed`, exit 0. The pre-existing three tests must still pass.

- [ ] **Step 5: Commit** (only these files — the tree has uncommitted bench deltas):

```bash
git add vv/unit/test_i2c_host_proto.c vv/unit/Makefile \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host_proto.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/i2c_host_proto.c
git commit -m "Add V5.2-compatible I2C host protocol core with host-side tests

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: HAL slave glue + firmware wiring (I2C1 = pure host slave)

> **Revised 2026-08-15:** the EEPROM moved to I2C3, so I2C1 is a pure slave.
> No listen suspend/resume API exists any more.

**Files:**
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host.h`
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/i2c_host.c`
- Modify: `.../Core/Src/main.c` (`MX_I2C1_Init` ~line 182: OwnAddress1; `main()` ~line 60: init call)
- Modify: `.../Core/Src/stm32g0xx_it.c` (add I2C1 IRQ handler)
- Modify: `.../Core/Src/applogic.c` (publish after `Inputs_Poll` in `AppLogic_Run`, ~line 148)

**Interfaces:**
- Consumes: Task 1's `I2CHostProto_*` API and `I2CHOST_ADDR_7BIT` / `I2CHOST_REF_MV` constants; `can_rxMessage.encoder_preset` / `.encoder_preset_update` volatile flags from `can_operation.h` (existing); `InputState` from `inputs.h`.
- Produces: `void I2CHost_Init(void)` (call after `MX_I2C1_Init`), `void I2CHost_Publish(const InputState *in)` (call each `Inputs_Poll` cycle). Task 4 relies on the whole slave being live.

- [ ] **Step 1: Write `Core/Inc/i2c_host.h`:**

```c
/* I2C1 host port: interrupt-driven slave at 0x51 serving the V5.2 ATtiny
 * protocol to an external master (Jetson) via connector P1. Pure slave -
 * the EEPROM lives on its own bus (I2C3). See i2c_host_proto.h for the
 * wire format and Docs/superpowers/specs/2026-08-14-i2c-host-compat-design.md. */
#ifndef INC_I2C_HOST_H_
#define INC_I2C_HOST_H_

#include "inputs.h"

/* Copy the STM32 UID into the reply state and arm slave listen mode.
 * Call once, after MX_I2C1_Init(). */
void I2CHost_Init(void);

/* Refresh the snapshot the slave ISR serves from. Called from the main
 * loop after Inputs_Poll; the ISR never touches InputState directly. */
void I2CHost_Publish(const InputState *in);

#endif /* INC_I2C_HOST_H_ */
```

- [ ] **Step 2: Write `Core/Src/i2c_host.c`:**

```c
#include "i2c_host.h"
#include "i2c_host_proto.h"
#include "can_operation.h"   /* encoder preset flags                     */
#include "main.h"            /* hi2c1                                    */

/* Snapshot written by the main loop, read by the slave ISR. Same volatile
 * discipline as CAN_RXMessage: without it a hoisted load serves stale
 * bytes forever. */
static volatile I2CHostProtoState s_state;
static volatile uint8_t  s_last_cmd = I2CHOST_CMD_READ_ALL; /* ATtiny default */
static uint16_t s_last_mv = 0U;

static uint8_t s_rx[3];                  /* cmd + optional 2-byte payload   */
static uint8_t s_tx[I2CHOST_MAX_REPLY];

void I2CHost_Init(void)
{
    const uint32_t w[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) {
        s_state.uid[i] = (uint8_t)(w[i / 4U] >> (8U * (i % 4U)));
    }
    s_state.ref_mv = I2CHOST_REF_MV;
    HAL_NVIC_SetPriority(I2C1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_IRQn);
    (void)HAL_I2C_EnableListen_IT(&hi2c1);
}

void I2CHost_Publish(const InputState *in)
{
    s_last_mv        = I2CHostProto_RotaryMv(in->rotary_pos, s_last_mv);
    s_state.knob_mv  = s_last_mv;
    s_state.encoder  = in->encoder_pos;
    s_state.button   = in->encoder_button;
}

/* ---- HAL slave callbacks ------------------------------------------------ */

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t dir, uint16_t addr)
{
    (void)addr;
    if (hi2c->Instance != I2C1) return;

    if (dir == I2C_DIRECTION_TRANSMIT) {
        /* Master writes: first byte is the command. */
        (void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx[0], 1U, I2C_NEXT_FRAME);
    } else {
        /* Master reads: serve the reply for the last command. Copy the
         * volatile snapshot to a plain struct for the pure builder. */
        I2CHostProtoState st;
        st.knob_mv = s_state.knob_mv;  st.ref_mv  = s_state.ref_mv;
        st.encoder = s_state.encoder;  st.button  = s_state.button;
        for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) st.uid[i] = s_state.uid[i];

        uint8_t len = I2CHostProto_BuildReply(s_last_cmd, &st, s_tx);
        /* Pad to the full buffer: a master that over-reads gets 0xFF
         * instead of clock-starving the bus. */
        for (uint8_t i = len; i < I2CHOST_MAX_REPLY; i++) s_tx[i] = 0xFFU;
        (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, s_tx, I2CHOST_MAX_REPLY,
                                            I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    if (hi2c->pBuffPtr == &s_rx[1]) {
        /* Command byte just landed. */
        s_last_cmd = s_rx[0];
        if (s_last_cmd == I2CHOST_CMD_RW_ENCODER) {
            /* An encoder write may follow: cmd + 2 LE bytes. */
            (void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx[1], 2U,
                                               I2C_NEXT_FRAME);
        }
        /* Other commands carry no payload; STOP lands in ListenCplt. */
    } else {
        /* Payload complete: route through the same preset path CMD_ENCODER
         * uses on CAN, so there is one apply site in applogic.c. */
        can_rxMessage.encoder_preset =
            I2CHostProto_ParseEncoderWrite(&s_rx[1]);
        can_rxMessage.encoder_preset_update = 1U;
    }
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    (void)HAL_I2C_EnableListen_IT(hi2c);   /* re-arm for the next START */
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    /* AF here is the normal end of a slave transmit (master NACKs its
     * final byte). Anything else is also recovered the same way: clear
     * and re-arm listen. */
    (void)HAL_I2C_EnableListen_IT(hi2c);
}
```

- [ ] **Step 3: Wire it in.**
In `main.c` `MX_I2C1_Init` (~line 182) change `hi2c1.Init.OwnAddress1 = 0;` to:

```c
    hi2c1.Init.OwnAddress1      = (uint16_t)(I2CHOST_ADDR_7BIT << 1);
```

with `#include "i2c_host_proto.h"` added to `main.c`'s includes, and in `main()` after `MX_I2C1_Init();` (~line 60) add:

```c
    I2CHost_Init();      /* slave listen for the V5.2-compatible host port */
```

(plus `#include "i2c_host.h"`). In `stm32g0xx_it.c` add alongside the existing handlers:

```c
/* I2C1 host port: event and error share one vector on the G0. */
void I2C1_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&hi2c1);
    HAL_I2C_ER_IRQHandler(&hi2c1);
}
```

with `extern I2C_HandleTypeDef hi2c1;` next to the file's existing extern declarations (or rely on `main.h`, which already declares it). In `applogic.c`, immediately after the `Inputs_Poll(&sm->inputs);` call in `AppLogic_Run` (~line 148) add:

```c
    I2CHost_Publish(&sm->inputs);
```

(plus `#include "i2c_host.h"` at the top).

- [ ] **Step 4: Build headless, verify clean.**

Run:
```bash
/c/ST/STM32CubeIDE_1.18.0/STM32CubeIDE/stm32cubeidec.exe -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data <scratch workspace dir> \
  -import STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG \
  -cleanBuild STM32G0_BUTTONBOARD_PROG/Debug
```
Expected: `Build Finished. 0 errors, 0 warnings.` (any new warning is a regression against `vv/baseline.txt`).

- [ ] **Step 5: Commit** (named files only — bench deltas in `main.c` stay uncommitted, so stage `main.c` by hunk: `git add -p` selecting only the OwnAddress1/include/init hunks):

```bash
git add STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/i2c_host.c \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/stm32g0xx_it.c \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/applogic.c
git add -p STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/main.c
git commit -m "Serve the V5.2 host I2C protocol as a slave on I2C1

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Move the EEPROM to I2C3 and propagate its status

> **Revised 2026-08-15:** replaces the old "listen coexistence" task. The
> `.ioc` already carries I2C3 on PA6/PA7 at 100 kHz (`Timing 0x10A077A8`);
> the hand-written C must catch up.

**Files:**
- Modify: `.../Core/Inc/board_pins.h` (I2C section ~lines 58-66: rename + add I2C3 pins)
- Modify: `.../Core/Src/main.c` (add `hi2c3` handle + `MX_I2C3_Init`, call it in `main()`)
- Modify: `.../Core/Inc/main.h` (~line 21: `extern hi2c3`)
- Modify: `.../Core/Src/stm32g0xx_hal_msp.c` (`HAL_I2C_MspInit` ~lines 60-89: add I2C3 branch)
- Modify: `.../Core/Inc/eeprom_driver.h` (~lines 37-45: return types)
- Modify: `.../Core/Src/eeprom_driver.c` (`hi2c1` → `hi2c3` everywhere; status returns; ACK poll ~lines 73-80)
- Modify: `.../Core/Src/applogic.c` (`service_commands` ~lines 70-81, `STATE_LOAD_CONFIG` ~line 162)

**Interfaces:**
- Consumes: nothing from Task 2 (independent buses).
- Produces: `hi2c3` handle; `bool EEPROM_Write_Config(const Config *c)` and `bool EEPROM_Read_Config(Config *c)` (were `void`); `applogic.c` sets `ERR_EEPROM` on a failed save.

- [ ] **Step 1: Pins.** In `board_pins.h` replace the I2C section with two named buses:

```c
/* -------------------------------------------------------------------------
 * I2C1 — host port to the Jetson via P1 (STM32 is a slave at 0x51).
 * R11/R12 give 10 k on-board.
 * ------------------------------------------------------------------------- */
#define HOST_SCL_Pin            GPIO_PIN_6      /* PB6  AF6 I2C1_SCL         */
#define HOST_SCL_GPIO_Port      GPIOB
#define HOST_SDA_Pin            GPIO_PIN_7      /* PB7  AF6 I2C1_SDA         */
#define HOST_SDA_GPIO_Port      GPIOB

/* -------------------------------------------------------------------------
 * I2C3 — AT24C256 config EEPROM (STM32 is the only master), 100 kHz.
 * ------------------------------------------------------------------------- */
#define EEPROM_SCL_Pin          GPIO_PIN_7      /* PA7  AF6 I2C3_SCL         */
#define EEPROM_SCL_GPIO_Port    GPIOA
#define EEPROM_SDA_Pin          GPIO_PIN_6      /* PA6  AF6 I2C3_SDA         */
#define EEPROM_SDA_GPIO_Port    GPIOA
```

and update the existing `HAL_I2C_MspInit` I2C1 branch in `stm32g0xx_hal_msp.c` to use `HOST_SCL_Pin | HOST_SDA_Pin` (the old `I2C_SCL_Pin`/`I2C_SDA_Pin` names are removed).

- [ ] **Step 2: I2C3 peripheral.** In `main.c` add next to `hi2c1`:

```c
I2C_HandleTypeDef   hi2c3;      /* EEPROM bus, PA6/PA7, 100 kHz */
```

and a new init (call it in `main()` right after `MX_I2C1_Init();`):

```c
/** I2C3: AT24C256 EEPROM, standard-mode 100 kHz from the 60 MHz kernel clock. */
static void MX_I2C3_Init(void)
{
    hi2c3.Instance              = I2C3;
    hi2c3.Init.Timing           = 0x10A077A8;
    hi2c3.Init.OwnAddress1      = 0;
    hi2c3.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2      = 0;
    hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c3.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c3) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK) {
        Error_Handler();
    }
}
```

`main.h` gets `extern I2C_HandleTypeDef hi2c3;` beside `hi2c1`. In `stm32g0xx_hal_msp.c` `HAL_I2C_MspInit`, add an `else if (hi2c->Instance == I2C3)` branch mirroring the I2C1 one:

```c
    else if (hi2c->Instance == I2C3)
    {
        /* I2C3 has no CCIPR kernel-clock mux on the G0B1 - it always runs
         * from PCLK1, so there is no HAL_RCCEx_PeriphCLKConfig step. */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin       = (uint16_t)(EEPROM_SCL_Pin | EEPROM_SDA_Pin);
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull      = GPIO_PULLUP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF6_I2C3;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        __HAL_RCC_I2C3_CLK_ENABLE();
    }
```

(Verified against the vendored HAL: `GPIO_AF6_I2C3` and `__HAL_RCC_I2C3_CLK_ENABLE()` exist; `RCC_PERIPHCLK_I2C3` does **not** — only I2C1/I2C2 have clock-source selectors on this part.)

- [ ] **Step 3: EEPROM driver.** In `eeprom_driver.c` change `extern I2C_HandleTypeDef hi2c1;` to `hi2c3` and every `&hi2c1` to `&hi2c3`. In `eeprom_driver.h` the config API becomes:

```c
#include <stdbool.h>
bool EEPROM_Write_Config(const Config *config);   /* true = persisted        */
bool EEPROM_Read_Config(Config *config);          /* true = read succeeded   */
```

`EEPROM_Write_Config` stops discarding statuses (`EEPROM_Read_Config` mirrors it with `HAL_I2C_Mem_Read`):

```c
bool EEPROM_Write_Config(const Config *config)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c3, EEPROM_ADDR, 0U, 2,
                                             (uint8_t *)config, sizeof(Config), 100U);
    if (st == HAL_OK) {
        /* AT24C256 internal write cycle is ~5 ms: poll with HAL's own
         * trials loop instead of the old 10 fast NACK probes. */
        st = HAL_I2C_IsDeviceReady(&hi2c3, EEPROM_ADDR, 300U, 10U);
    }
    return st == HAL_OK;
}
```

Delete the old free-standing ACK-poll retry loop (lines 73-80) and the commented-out `HAL_Delay(5)`; the `IsDeviceReady(…, 300, 10)` call replaces both.

- [ ] **Step 4: Consume the status in `applogic.c` `service_commands`:**

```c
    if (can_rxMessage.eeprom_save != 0U) {
        can_rxMessage.eeprom_save = 0U;
        sm->config.magic = EEPROM_CFG_MAGIC;
        /* ...existing field stamping unchanged... */
        if (EEPROM_Write_Config(&sm->config)) {
            sm->errorMask &= (uint8_t)~ERR_EEPROM;
        } else {
            /* Host must see the failed save: EEPROMDATA still echoes the
             * RAM copy, so this bit is the only truth signal. */
            sm->errorMask |= ERR_EEPROM;
        }
    }
```

and the boot-time read in `STATE_LOAD_CONFIG` treats a `false` return like a bad magic (load defaults + `ERR_EEPROM`).

- [ ] **Step 5: Build headless (same command as Task 2 Step 4).** Expected: 0 errors, 0 warnings.

- [ ] **Step 6: Re-run host tests** — `make -C vv/unit clean all` — expected all pass (no host-side files touched; this catches accidental proto edits).

- [ ] **Step 7: Commit** (stage `main.c` and `stm32g0xx_hal_msp.c` by hunk with `git add -p` — both carry uncommitted bench-only deltas that must stay out):

```bash
git add STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/board_pins.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/main.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/eeprom_driver.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/eeprom_driver.c \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/applogic.c
git add -p STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/main.c
git add -p STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/stm32g0xx_hal_msp.c
git commit -m "Move the config EEPROM to I2C3 and report its I2C failures

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Bench verification — Raspberry Pi 4 as host AND EEPROM emulator

> **Revised 2026-08-15:** no standalone I2C master is available; a Raspberry
> Pi 4 plays both roles. It is a genuine host-class machine (the same
> `smbus2` loop will later run on the Jetson) and Linux's `i2c-slave-eeprom`
> module lets a second, bit-banged bus answer as the AT24C256 — with the
> emulated EEPROM's contents readable from a sysfs file, which a real chip
> cannot offer.

**Files:**
- Create: `Tools/bench/pi_i2c_host_emu.py` (host-side V5.2 protocol exerciser, python3 + smbus2)
- Create: `Tools/bench/pi_setup_i2c_bench.sh` (one-shot Pi configuration)
- No firmware changes. Uses the Task 2/3 build and the CAN flash path.

**Interfaces:**
- Consumes: everything from Tasks 1-3; the acceptance gate from the spec's Testing section.

**Wiring (5 wires from the Pi 40-pin header to the Nucleo, all 3.3 V logic):**

| Role | Pi pin | Pi GPIO | → Nucleo pin | STM32 |
|---|---|---|---|---|
| Host bus SDA | 3 | GPIO2 (i2c-1 SDA) | PB7 (CN7) | `HOST_SDA` |
| Host bus SCL | 5 | GPIO3 (i2c-1 SCL) | PB6 (CN10) | `HOST_SCL` |
| EEPROM bus SDA | 16 | GPIO23 (bit-bang) | PA6 (CN10) | `EEPROM_SDA` |
| EEPROM bus SCL | 18 | GPIO24 (bit-bang) | PA7 (CN10) | `EEPROM_SCL` |
| Ground | 6 | GND | any GND | |

Pull-ups: the Pi's i2c-1 already has 1.8 kΩ on-board (host bus needs nothing extra). The bit-banged bus has none — add **3.3–4.7 kΩ from GPIO23 and GPIO24 to the Pi's 3V3 (pin 1)**. Do NOT power the Nucleo from the Pi; it stays on its own USB.

- [ ] **Step 1: Configure the Pi.** Copy and run `Tools/bench/pi_setup_i2c_bench.sh` (contents below) once, then reboot the Pi:

```bash
#!/usr/bin/env bash
# One-shot Raspberry Pi 4 setup: hardware I2C1 as the host bus, a bit-banged
# second bus (GPIO23/24) hosting a kernel-emulated 24C256 for the STM32.
set -euo pipefail
sudo raspi-config nonint do_i2c 0                       # enable i2c-1 (GPIO2/3)
CFG=/boot/firmware/config.txt; [ -f "$CFG" ] || CFG=/boot/config.txt
grep -q '^dtoverlay=i2c-gpio' "$CFG" || \
  echo 'dtoverlay=i2c-gpio,bus=3,i2c_gpio_sda=23,i2c_gpio_scl=24,i2c_gpio_delay_us=2' | sudo tee -a "$CFG"
grep -q '^i2c-dev'          /etc/modules || echo i2c-dev          | sudo tee -a /etc/modules
grep -q '^i2c-slave-eeprom' /etc/modules || echo i2c-slave-eeprom | sudo tee -a /etc/modules
sudo apt-get install -y -q python3-smbus2 i2c-tools >/dev/null 2>&1 || sudo pip3 install smbus2
echo "Reboot, then: sudo bash -c 'echo slave-24c256 0x1050 > /sys/bus/i2c/devices/i2c-3/new_device'"
echo "Backing file: /sys/bus/i2c/devices/3-1050/slave-eeprom"
```

After reboot, instantiate the emulated EEPROM (0x1050 = slave flag 0x1000 | address 0x50) and check both buses:

```bash
sudo bash -c 'echo slave-24c256 0x1050 > /sys/bus/i2c/devices/i2c-3/new_device'
i2cdetect -y 1        # host bus: expect 51 once the STM32 app is running
ls -la /sys/bus/i2c/devices/3-1050/slave-eeprom     # 32768-byte backing file
```

- [ ] **Step 2: Flash the Task 3 build over CAN** (bootloader or app may be running — 0x780 CONNECT resets it):

```bash
/c/Users/User/Documents/GitHub/openblt/Host/BootCommander.exe -s=xcp -t=xcp_can \
  -d=peak_pcanusb -b=500000 -tid=780 -rid=781 \
  STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Debug/STM32G0_BUTTONBOARD_PROG.srec
```
Expected: all `[OK]`; 0x7A0/0x7A1 broadcasts resume at 100 ms; `i2cdetect -y 1` on the Pi shows `51`.

- [ ] **Step 3: Host emulator** — `Tools/bench/pi_i2c_host_emu.py`:

```python
#!/usr/bin/env python3
"""V5.2 ATtiny-protocol host exerciser for the ButtonBoard V5.5 STM32 slave.

Runs the exact byte sequences the Jetson-side V5.2 code uses, against the
STM32 at 0x51 on the Pi's i2c-1. Usage:
    ./pi_i2c_host_emu.py poll            # READ_ALL loop, prints decoded fields
    ./pi_i2c_host_emu.py write-enc -100  # RW_ENCODER preset (LE), then reads back
    ./pi_i2c_host_emu.py version         # READ_VERSION -> (major, minor)
    ./pi_i2c_host_emu.py uid             # READ_UID -> 10 bytes
    ./pi_i2c_host_emu.py overread        # read 40 bytes after READ_ALL: bytes 7.. must be 0xFF
"""
import struct, sys, time
from smbus2 import SMBus, i2c_msg

ADDR = 0x51
CMD_READ_ALL, CMD_RW_ENC, CMD_VER, CMD_UID = 0x09, 0x80, 0xFE, 0x10

def xfer(bus, cmd, nread, payload=b""):
    """Write cmd(+payload), repeated-START read nread bytes - the ATtiny pattern."""
    w = i2c_msg.write(ADDR, bytes([cmd]) + payload)
    r = i2c_msg.read(ADDR, nread)
    bus.i2c_rdwr(w, r)
    return bytes(r)

def read_all(bus):
    b = xfer(bus, CMD_READ_ALL, 7)
    knob, ref, enc = struct.unpack(">HHh", b[:6])      # big-endian, as the ATtiny sent
    return knob, ref, enc, b[6]

def main():
    op = sys.argv[1] if len(sys.argv) > 1 else "poll"
    with SMBus(1) as bus:
        if op == "poll":
            while True:
                knob, ref, enc, btn = read_all(bus)
                print(f"knob={knob:4d}mV ref={ref}mV enc={enc:6d} btn={btn}", flush=True)
                time.sleep(0.1)
        elif op == "write-enc":
            val = int(sys.argv[2])
            xfer(bus, CMD_RW_ENC, 0, struct.pack("<h", val))     # LE write - the asymmetry
            time.sleep(0.05)
            (enc,) = struct.unpack(">h", xfer(bus, CMD_RW_ENC, 2))  # BE read
            print(f"wrote {val}, read back {enc}", "OK" if enc == val else "MISMATCH")
        elif op == "version":
            print("version", tuple(xfer(bus, CMD_VER, 2)))
        elif op == "uid":
            print("uid", xfer(bus, CMD_UID, 10).hex())
        elif op == "overread":
            b = xfer(bus, CMD_READ_ALL, 40)
            print("first 7:", b[:7].hex(), "pad all 0xFF:", all(x == 0xFF for x in b[7:]))
        else:
            sys.exit(__doc__)

if __name__ == "__main__":
    main()
```

The `overread` case exercises Task 2's 1-byte pad re-arm: a 40-byte read after READ_ALL must return the 7 real bytes then 33 × 0xFF with no bus hang.

- [ ] **Step 4: Acceptance checks** (each must hold; record pass/fail):
  1. `poll`: `ref=3300`, `enc` tracks the physical encoder ±1 per detent, and CAN 0x7A0 shows the identical count concurrently.
  2. `write-enc -100`: readback −100 **and** CAN 0x7A0 jumps to −100 (one apply path).
  3. `version` → `(2, 0)`; `uid` → 10 stable bytes across runs.
  4. `overread` → 7 real bytes then all-0xFF, and the next `poll` still works (no stuck slave).
  5. Rotary: ground `ROT_SW_3` (PB2) → `knob=1650`; release → holds 1650.
  6. **EEPROM round-trip through the emulator:** with the emulated EEPROM instantiated, power-cycle the Nucleo. First boot: DEVSTATUS (CAN 0x7A3 byte 1) has ERR_EEPROM set (blank chip → bad magic → defaults). Send CAN `0x793 [01]` (save); ERR_EEPROM clears. On the Pi: `xxd -l 16 /sys/bus/i2c/devices/3-1050/slave-eeprom` shows `42 4b ...` (magic 'KB' 0x4B42 LE) — the STM32's bytes, verbatim. Power-cycle the Nucleo again: ERR_EEPROM stays clear (config persisted and read back).
  7. **Failure truth signal:** remove the emulated device (`echo 0x1050 > /sys/bus/i2c/devices/i2c-3/delete_device`), send `0x793 [01]` again → ERR_EEPROM **sets** (Task 3's status propagation). Re-add the device, save again → clears.
  8. **Bus isolation:** run `poll` continuously while doing check 6/7 — the host stream never stalls or errors; a logic analyser (or the Pi's own `i2cdetect -y 3` timing) shows EEPROM traffic only on PA6/PA7.

- [ ] **Step 5: Record results.** Append a dated results section to this plan file listing pass/fail per check with the observed values; any failure goes through the systematic-debugging skill, not an ad-hoc patch. Commit `Tools/bench/*` and the results:

```bash
git add Tools/bench/pi_i2c_host_emu.py Tools/bench/pi_setup_i2c_bench.sh Docs/superpowers/plans/2026-08-14-i2c-host-compat.md
git commit -m "Add the Raspberry Pi host/EEPROM emulator bench for the I2C host port

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

**Known limits of this bench:** the kernel EEPROM slave does not emulate the AT24C256's ~5 ms write-cycle NACK, so the `IsDeviceReady(…, 300, 10)` poll returns on the first trial here — its timing is only proven on the real board. The Pi 4's hardware I2C1 clock-stretch bug is irrelevant on the host bus (the STM32 slave stretches only within one byte time); if the emulated-EEPROM link is flaky, lower `i2c_gpio_delay_us` sensitivity by raising it to 5 (≈50 kHz).

## Self-Review

- **Spec coverage:** protocol table → Task 1; dual-role slave, snapshot, preset reuse → Task 2; EEPROM coexistence + ERR_EEPROM (spec's "subsumes finding #5") → Task 3; bench acceptance incl. save-under-polling → Task 4. Rotary hold-last → Tasks 1/2. Version/UID defaults → Task 1 constants. No gaps found.
- **Placeholder scan:** none; the one "existing field stamping unchanged" comment refers to code that already exists at the cited lines, not deferred work.
- **Type consistency:** `I2CHostProtoState`, `I2CHostProto_BuildReply/RotaryMv/ParseEncoderWrite`, `I2CHost_Init/Publish`, `bool EEPROM_Write_Config/Read_Config` used identically across tasks.

## Task 4 results — 2026-08-15, Nucleo-G0B1RE + Raspberry Pi 4 (`ubuntu@100.124.51.54`)

**Bench as actually built** (differs from the plan text above): the Pi's kernel
(`6.18.34+rpt-rpi-v8`, Raspberry Pi OS Trixie) ships no `i2c-slave-eeprom`
module, and its `i2c-0` is the camera mux (an `imx219` overlay is active), so
only `i2c-1` (GPIO2/3, header pins 3/5) is a usable bus. It is wired as the
**host** bus to PB7 (SDA, CN7-21) / PB6 (SCL, CN10-17). PA6/PA7 (I2C3, EEPROM)
are left unconnected — the "no chip" case. Firmware = branch HEAD `cea8855`
build (31024 B), flashed over CAN via BootCommander. Host script:
`Tools/bench/pi_i2c_host_emu.py` (staged on the Pi as `~/pi_i2c_host_emu.py`).

Two wiring faults were found by pulsing each Pi line with `pinctrl` and reading
STM32 `GPIOB->IDR` over SWD (0x50000410) — a technique worth keeping: an
all-addresses `i2cdetect` result means SDA is held/undriven, not "many slaves".

| # | Check | Result | Observed |
|---|---|---|---|
| 0 | `i2cdetect -y 1` | **PASS** | exactly one address: `51` |
| 1 | READ_ALL poll: `ref=3300`, encoder tracks, CAN 0x7A0 identical | **PASS** | `knob=3300 ref=3300 enc=0 btn=1`; enc identical on CAN |
| 2 | `write-enc` (LE write, BE read) and CAN sees it | **PASS** | −100, 222, −333 all read back; CAN 0x7A0 = −333 |
| 3 | `version` = (2,0); `uid` 10 bytes, stable | **PASS** | `(2, 0)`; `0f001800145041394b33` twice |
| 4 | over-read 40 B → 7 real + 33×0xFF, slave not stuck | **PASS** | `0ce40ce400de01` + all-0xFF; next poll fine |
| 5 | rotary ROT_SW_3 → 1650, hold on release | **PASS** (2026-08-15) | Jordan wired the real rotary switch to the Nucleo; positions read correctly over CAN (`rotary_pos` 0..6), confirming the 7-position mapping and closing spec review note #1 |
| 6 | EEPROM round-trip | **PASS** (2026-08-15, real AT24C256 on I2C3) | save `2a 05 01` → hardware reset → read back `42 4b 2a 05 01`, `ERR_EEPROM` clear. Initially FAILED: I2C3 was configured AF6 instead of AF9 on PA6/PA7 and wedged BUSY on its first START — fixed in `9b3e264` |
| 7 | failed save sets `ERR_EEPROM` (truth signal) | **PASS** | CAN `0x793 [01]` with no chip → DEVSTATUS err stays `0x03`; 0x7A0 cadence unaffected (10/s) |
| 8 | bus isolation / concurrency | **PASS** | 40 host polls, 0 errors, while CAN 0x7A0 max gap = 100 ms exactly |

Notes: `btn=1` and `knob=3300` are floating-pin artefacts on the Nucleo (PA2
and the ROT_SW lines have no external pulls here), not defects. Bench-only
firmware deltas (HSI clock, CSS off, encoder pull-ups) remain uncommitted.
