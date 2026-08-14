# V5.2-Compatible I2C Host Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ButtonBoard V5.5 firmware answer the V5.2 ATtiny's I2C slave protocol at 7-bit address 0x51, so existing Jetson host code works unchanged.

**Architecture:** A pure protocol module (`i2c_host_proto.c`, no HAL includes, host-testable exactly like `battery.c`) builds every reply from a plain snapshot struct. A thin HAL glue module (`i2c_host.c`) runs I2C1 in dual role — interrupt-driven slave listen for the host, existing blocking master for the EEPROM — and suspends listen around EEPROM transactions. EEPROM driver return codes stop being discarded as part of this work.

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
- Create `.../Core/Inc/i2c_host.h` + `.../Core/Src/i2c_host.c` — HAL slave glue: listen management, callbacks, snapshot publish, suspend/resume for EEPROM master ops.
- Create `vv/unit/test_i2c_host_proto.c` — host tests compiling the real `i2c_host_proto.c`.
- Modify `vv/unit/Makefile` — new test target (battery pattern).
- Modify `.../Core/Src/main.c` (OwnAddress1, init call), `.../Core/Src/stm32g0xx_it.c` (I2C1 IRQ), `.../Core/Src/applogic.c` (publish + EEPROM error), `.../Core/Src/eeprom_driver.c` + `Inc/eeprom_driver.h` (status returns, ACK-poll fix, suspend/resume wrap).

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

### Task 2: HAL slave glue + firmware wiring

**Files:**
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/i2c_host.h`
- Create: `STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/i2c_host.c`
- Modify: `.../Core/Src/main.c` (`MX_I2C1_Init` ~line 182: OwnAddress1; `main()` ~line 60: init call)
- Modify: `.../Core/Src/stm32g0xx_it.c` (add I2C1 IRQ handler)
- Modify: `.../Core/Src/applogic.c` (publish after `Inputs_Poll` in `AppLogic_Run`, ~line 148)

**Interfaces:**
- Consumes: Task 1's `I2CHostProto_*` API; `can_rxMessage.encoder_preset` / `.encoder_preset_update` volatile flags from `can_operation.h` (existing); `InputState` from `inputs.h`.
- Produces: `void I2CHost_Init(void)` (call after `MX_I2C1_Init`), `void I2CHost_Publish(const InputState *in)` (call each `Inputs_Poll` cycle), `void I2CHost_SuspendListen(void)` / `void I2CHost_ResumeListen(void)` (Task 3 wraps EEPROM ops in these). Task 4 relies on the whole slave being live.

- [ ] **Step 1: Write `Core/Inc/i2c_host.h`:**

```c
/* Dual-role I2C1: interrupt-driven slave (host at 0x51) alongside the
 * existing blocking EEPROM master. See i2c_host_proto.h for the wire
 * format and Docs/superpowers/specs/2026-08-14-i2c-host-compat-design.md. */
#ifndef INC_I2C_HOST_H_
#define INC_I2C_HOST_H_

#include "inputs.h"

/* Copy the STM32 UID into the reply state and arm slave listen mode.
 * Call once, after MX_I2C1_Init(). */
void I2CHost_Init(void);

/* Refresh the snapshot the slave ISR serves from. Called from the main
 * loop after Inputs_Poll; the ISR never touches InputState directly. */
void I2CHost_Publish(const InputState *in);

/* Master transactions (EEPROM) may not run while listen is armed. Wrap
 * them in Suspend/Resume; the host sees a NACK meanwhile, identical to
 * polling a busy EEPROM. */
void I2CHost_SuspendListen(void);
void I2CHost_ResumeListen(void);

#endif /* INC_I2C_HOST_H_ */
```

- [ ] **Step 2: Write `Core/Src/i2c_host.c`:**

```c
#include "i2c_host.h"
#include "i2c_host_proto.h"
#include "can_operation.h"   /* encoder preset flags + hi2c1 via main.h  */
#include "main.h"

extern I2C_HandleTypeDef hi2c1;

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

void I2CHost_SuspendListen(void)
{
    (void)HAL_I2C_DisableListen_IT(&hi2c1);
}

void I2CHost_ResumeListen(void)
{
    (void)HAL_I2C_EnableListen_IT(&hi2c1);
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

with `extern I2C_HandleTypeDef hi2c1;` next to the file's existing extern declarations. In `applogic.c`, immediately after the `Inputs_Poll(&sm->inputs);` call in `AppLogic_Run` (~line 148) add:

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
git commit -m "Serve the V5.2 host I2C protocol as a slave on the shared bus

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: EEPROM status propagation + listen coexistence

**Files:**
- Modify: `.../Core/Inc/eeprom_driver.h` (~lines 37-45: return types)
- Modify: `.../Core/Src/eeprom_driver.c` (write/read/config functions, ACK poll ~lines 73-80)
- Modify: `.../Core/Src/applogic.c` (`service_commands` ~lines 70-81)

**Interfaces:**
- Consumes: `I2CHost_SuspendListen()` / `I2CHost_ResumeListen()` from Task 2.
- Produces: `bool EEPROM_Write_Config(const Config *c)` and `bool EEPROM_Read_Config(Config *c)` (were `void`); `applogic.c` sets `ERR_EEPROM` on a failed save.

- [ ] **Step 1: Change the driver.** In `eeprom_driver.h` the config API becomes:

```c
#include <stdbool.h>
bool EEPROM_Write_Config(const Config *config);   /* true = persisted     */
bool EEPROM_Read_Config(Config *config);          /* true = read + valid I2C */
```

In `eeprom_driver.c`, `EEPROM_Write_Config` wraps the transaction and stops discarding statuses (pattern below; `EEPROM_Read_Config` mirrors it with `HAL_I2C_Mem_Read`):

```c
bool EEPROM_Write_Config(const Config *config)
{
    HAL_StatusTypeDef st;
    I2CHost_SuspendListen();
    /* Two attempts: the Jetson is a second master on this bus, so a
     * transaction can lose arbitration (ARLO) and must retry once the
     * bus frees up. */
    for (uint8_t attempt = 0U; attempt < 2U; attempt++) {
        st = HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, 0U, 2,
                               (uint8_t *)config, sizeof(Config), 100U);
        if (st == HAL_OK) break;
        HAL_Delay(2);
    }
    if (st == HAL_OK) {
        /* AT24C256 internal write cycle is ~5 ms: poll with HAL's own
         * trials loop instead of the old 10 fast NACK probes. */
        st = HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDR, 300U, 10U);
    }
    I2CHost_ResumeListen();
    return st == HAL_OK;
}
```

Delete the old free-standing ACK-poll retry loop (lines 73-80) and the commented-out `HAL_Delay(5)`; the `IsDeviceReady(…, 300, 10)` call replaces both.

- [ ] **Step 2: Consume the status in `applogic.c` `service_commands`:**

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

- [ ] **Step 3: Build headless (same command as Task 2 Step 4).** Expected: 0 errors, 0 warnings.

- [ ] **Step 4: Re-run host tests** — `make -C vv/unit clean all` — expected all pass (no host-side files touched; this catches accidental proto edits).

- [ ] **Step 5: Commit:**

```bash
git add STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Inc/eeprom_driver.h \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/eeprom_driver.c \
    STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Core/Src/applogic.c
git commit -m "Propagate EEPROM I2C failures and coexist with host slave mode

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Bench verification on the Nucleo

**Files:**
- No firmware changes. Uses the Task 2/3 build, the CAN flash path, and an external I2C master (the V5.2 ATtiny board reflashed, or any Arduino) wired SDA→PB7, SCL→PB6, GND common, 3.3 V logic, 3.3-4.7 k pull-ups to 3V3.

**Interfaces:**
- Consumes: everything; this is the acceptance gate from the spec's Testing section.

- [ ] **Step 1: Flash the build over CAN** (board must be in bootloader or app running — the 0x780 CONNECT resets it):

```bash
/c/Users/User/Documents/GitHub/openblt/Host/BootCommander.exe -s=xcp -t=xcp_can \
  -d=peak_pcanusb -b=500000 -tid=780 -rid=781 \
  STM32G0B1_Applciationprog/STM32G0_BUTTONBOARD_PROG/Debug/STM32G0_BUTTONBOARD_PROG.srec
```
Expected: erase/program/finish all `[OK]`, then 0x7A0/0x7A1 broadcasts resume at 100 ms.

- [ ] **Step 2: Master test sketch** on the I2C master (Arduino-style):

```c
#include <Wire.h>
void setup() { Wire.begin(); Serial.begin(115200); }
void loop() {
  uint8_t b[7];
  Wire.beginTransmission(0x51); Wire.write(0x09); Wire.endTransmission();
  Wire.requestFrom(0x51, 7);
  for (int i = 0; i < 7 && Wire.available(); i++) b[i] = Wire.read();
  int knob = (b[0] << 8) | b[1], ref = (b[2] << 8) | b[3];
  int16_t enc = (b[4] << 8) | b[5];
  Serial.print("knob="); Serial.print(knob);
  Serial.print(" ref="); Serial.print(ref);
  Serial.print(" enc="); Serial.print(enc);
  Serial.print(" btn="); Serial.println(b[6]);
  delay(100);
}
```

- [ ] **Step 3: Acceptance checks** (each must hold):
  1. READ_ALL loop prints `ref=3300`, `enc` tracks the physical encoder ±1 per detent, and CAN 0x7A0 reports the identical count concurrently.
  2. Encoder write: master sends `{0x80, 0x9C, 0xFF}`; both the I2C `enc` readback and CAN 0x7A0 jump to −100.
  3. Version (`0xFE`) reads `2,0`; UID (`0x10`) returns 10 stable bytes.
  4. CAN `0x793 [01]` (EEPROM save) during continuous host polling: broadcasts continue, the host sees at most brief NACKs, and with no EEPROM fitted DEVSTATUS (0x7A3 byte 1) now sets `ERR_EEPROM` — the Task 3 truth signal.
  5. Rotary lines: ground `ROT_SW_3` (PB2) briefly — `knob` reads 1650 (pos 3 × 550); release → holds 1650 (hold-last rule).

- [ ] **Step 4: Record results.** Append a dated results section to `Docs/superpowers/plans/2026-08-14-i2c-host-compat.md` (this file) listing pass/fail per check, and report any failure back through the systematic-debugging skill rather than patching ad hoc.

---

## Self-Review

- **Spec coverage:** protocol table → Task 1; dual-role slave, snapshot, preset reuse → Task 2; EEPROM coexistence + ERR_EEPROM (spec's "subsumes finding #5") → Task 3; bench acceptance incl. save-under-polling → Task 4. Rotary hold-last → Tasks 1/2. Version/UID defaults → Task 1 constants. No gaps found.
- **Placeholder scan:** none; the one "existing field stamping unchanged" comment refers to code that already exists at the cited lines, not deferred work.
- **Type consistency:** `I2CHostProtoState`, `I2CHostProto_BuildReply/RotaryMv/ParseEncoderWrite`, `I2CHost_Init/Publish/SuspendListen/ResumeListen`, `bool EEPROM_Write_Config/Read_Config` used identically across tasks.
