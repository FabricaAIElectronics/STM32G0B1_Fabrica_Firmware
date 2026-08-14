/**
  ******************************************************************************
  * @file    inputs.h
  * @brief   Encoder, rotary switch, toggle and button acquisition for the
  *          calibration knob / button board V5.5.
  ******************************************************************************
  */

#ifndef INC_INPUTS_H_
#define INC_INPUTS_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>

/* Reported in InputState.rotary_pos when the switch lines do not currently
 * describe a single valid position. Chosen as 0xFF so a host reading the byte
 * cannot mistake it for position 0. */
#define ROTARY_POS_INVALID      0xFFU

/* --------------------------------------------------------------------------
 * Rotary decode timing
 * --------------------------------------------------------------------------
 * RS1 is make-before-break (shorting), so two ADJACENT lines are legitimately
 * high while the knob passes between detents. That is a transition, not a
 * fault, and the decoder holds the previous position through it.
 *
 * A pattern that is neither "exactly one line" nor "two adjacent lines" —
 * zero lines, or two lines that are not neighbours, or three or more — only
 * becomes an error once it has persisted for ROTARY_FAULT_MS. Without the
 * dwell, a single noisy sample during a fast turn would latch a fault.
 */
#define ROTARY_FAULT_MS         250U

/* Encoder button and the panel buttons are mechanical; require a level to be
 * stable for this long before it is published. */
#define BUTTON_DEBOUNCE_MS      20U

/* --------------------------------------------------------------------------
 * Snapshot of every input, rebuilt each Inputs_Poll() call.
 * --------------------------------------------------------------------------
 * `struct InputState` is forward-declared in can_operation.h; keep the tag
 * name in step if this is ever renamed.
 */
typedef struct InputState {
    int16_t  encoder_pos;       /* signed detent count, wraps at +-32767     */
    uint8_t  encoder_button;    /* 1 = pressed (pin is active low)           */

    uint8_t  rotary_pos;        /* 0..6, or ROTARY_POS_INVALID               */
    uint8_t  rotary_raw;        /* bits 0..6 = ROT_SW_0..6 as read           */

    uint8_t  toggle_mask;       /* bits 0..3 = SW_1_3V3 .. SW_4_3V3          */
    uint8_t  button_mask;       /* bits 0..5 = Button_3V3_1..6, 1 = pressed  */
    uint8_t  button_int_mask;   /* bits 0..3 = Button_3V3_int_1..4           */

    bool     rotary_fault;      /* decode has been invalid for > dwell       */
} InputState;

/* Start TIM2 in encoder mode and seed the debouncers from the current pin
 * levels, so the first published snapshot is not a spurious edge. */
void Inputs_Init(InputState *in);

/* Sample everything. Cheap enough to call from the main loop at 1 kHz; the
 * debouncers are tick-based, not call-count based, so the rate does not
 * change the timing. */
void Inputs_Poll(InputState *in);

/* Force the encoder count to `pos` (CMD_ENCODER). Writes TIM2->CNT directly
 * so the hardware and the reported value cannot drift apart. */
void Inputs_SetEncoder(InputState *in, int16_t pos);

#endif /* INC_INPUTS_H_ */
