/**
  ******************************************************************************
  * @file    inputs.c
  * @brief   Encoder, rotary switch, toggle and button acquisition.
  *
  * ---------------------------------------------------------------------------
  * What the hardware does, and why the pull configuration is what it is
  * ---------------------------------------------------------------------------
  *
  * Encoder (ENC1, PEC11L-4120K-S0020)
  *   Common pin C is grounded; A and B carry 1 k pull-ups to VCC (R32, R33).
  *   So both channels idle high and pull low through the detent. PA0/PA1 are
  *   TIM2_CH1/CH2 on AF2, so the quadrature decode is done by the timer in
  *   hardware with x4 counting and the maximum input filter. The part gives
  *   one full quadrature cycle per detent, hence exactly 4 counts per detent
  *   — the reported position divides that back down.
  *   GPIO pull: NOPULL, the 1 k externals already define the idle level.
  *
  * Rotary (RS1, ALPS SRBV170501, 7-position, make-before-break)
  *   Common (pin 1) is tied to VCC through R29 = 1 k. Each position contact
  *   leaves through 100 R (RN7/RN9) and carries its own 10 k pull-down to
  *   GND (R24-R28, R30, R31). A selected position therefore reads
  *   3.3 x 10k/11k = 3.0 V (high) and every unselected line reads 0 V.
  *   GPIO pull: NOPULL — the 10 k externals do the job, and adding the ~40 k
  *   internal in parallel would only shift the divider for no benefit.
  *
  * Buttons (Button_3V3_1..6, Button_3V3_int_1..4)
  *   5 V contacts with 10 k pull-ups to +5 V (RN1/RN2), brought down through
  *   the LSF0108 translators. Pressed = LOW.
  *   GPIO pull: NOPULL. An internal pull-up here would fight the translator's
  *   pass-FET, which can only pull down.
  *
  * Toggles (SW_1_3V3 .. SW_4_3V3)
  *   After the rev-2 change to 100 R, both positions are hard-driven — VCC
  *   through 100 R closed, GND through 100 R open.
  *   GPIO pull: NOPULL.
  ******************************************************************************
  */

#include "inputs.h"
#include "board_pins.h"
#include "encoder_math.h"
#include "main.h"
#include <string.h>

extern TIM_HandleTypeDef htim2;

/* Set to 1 if turning the knob clockwise decrements. Which way TIM2 counts
 * depends on how A and B landed on the connector, and that is easier to check
 * on the bench than to derive from the drawing. */
#define ENCODER_INVERT      0

/* PEC11L-4120K-S0020: one quadrature cycle per detent, x4 decoding.
 * ENCODER_COUNTS_PER_DETENT and the count->detent binning live in
 * encoder_math.h so the vv host harness can assert them directly. */

/* --------------------------------------------------------------------------
 * Per-bit debouncer
 * --------------------------------------------------------------------------
 * A bit is published only once it has held its new value for `ms`. The timer
 * is per bit rather than per mask so that one chattering contact cannot delay
 * the reporting of the others — which is exactly what a single shared timer
 * would do on a panel where several buttons get pressed together.
 */
typedef struct {
    uint8_t  stable;
    uint8_t  candidate;
    uint32_t changed_at[8];
} Debounce;

static Debounce s_buttons;
static Debounce s_buttons_int;
static Debounce s_toggles;
static Debounce s_enc_button;

static uint8_t debounce_update(Debounce *d, uint8_t raw, uint8_t nbits,
                               uint32_t now, uint32_t ms)
{
    for (uint8_t i = 0U; i < nbits; i++) {
        const uint8_t mask = (uint8_t)(1U << i);
        const uint8_t rawbit = (uint8_t)(raw & mask);

        if ((d->candidate & mask) != rawbit) {
            /* Level moved — restart this bit's dwell. */
            d->candidate = (uint8_t)((d->candidate & ~mask) | rawbit);
            d->changed_at[i] = now;
        } else if (((d->stable & mask) != rawbit) &&
                   ((now - d->changed_at[i]) >= ms)) {
            d->stable = (uint8_t)((d->stable & ~mask) | rawbit);
        }
    }
    return d->stable;
}

static void debounce_seed(Debounce *d, uint8_t raw)
{
    d->stable = raw;
    d->candidate = raw;
    memset(d->changed_at, 0, sizeof(d->changed_at));
}

/* --------------------------------------------------------------------------
 * Raw sampling helpers
 * -------------------------------------------------------------------------- */

/* Buttons and the encoder switch are active low, so invert on read: the rest
 * of the firmware and the CAN protocol both use 1 = pressed. */
static inline uint8_t read_active_low(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static inline uint8_t read_active_high(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t sample_buttons(void)
{
    uint8_t m = 0U;
    m |= (uint8_t)(read_active_low(BUTTON_1_GPIO_Port, BUTTON_1_Pin) << 0);
    m |= (uint8_t)(read_active_low(BUTTON_2_GPIO_Port, BUTTON_2_Pin) << 1);
    m |= (uint8_t)(read_active_low(BUTTON_3_GPIO_Port, BUTTON_3_Pin) << 2);
    m |= (uint8_t)(read_active_low(BUTTON_4_GPIO_Port, BUTTON_4_Pin) << 3);
    m |= (uint8_t)(read_active_low(BUTTON_5_GPIO_Port, BUTTON_5_Pin) << 4);
    m |= (uint8_t)(read_active_low(BUTTON_6_GPIO_Port, BUTTON_6_Pin) << 5);
    return m;
}

static uint8_t sample_buttons_int(void)
{
    uint8_t m = 0U;
    m |= (uint8_t)(read_active_low(BUTTON_INT_1_GPIO_Port, BUTTON_INT_1_Pin) << 0);
    m |= (uint8_t)(read_active_low(BUTTON_INT_2_GPIO_Port, BUTTON_INT_2_Pin) << 1);
    m |= (uint8_t)(read_active_low(BUTTON_INT_3_GPIO_Port, BUTTON_INT_3_Pin) << 2);
    m |= (uint8_t)(read_active_low(BUTTON_INT_4_GPIO_Port, BUTTON_INT_4_Pin) << 3);
    return m;
}

/* Toggles read high when the switch selects the VCC side. */
static uint8_t sample_toggles(void)
{
    uint8_t m = 0U;
    m |= (uint8_t)(read_active_high(TOGGLE_1_GPIO_Port, TOGGLE_1_Pin) << 0);
    m |= (uint8_t)(read_active_high(TOGGLE_2_GPIO_Port, TOGGLE_2_Pin) << 1);
    m |= (uint8_t)(read_active_high(TOGGLE_3_GPIO_Port, TOGGLE_3_Pin) << 2);
    m |= (uint8_t)(read_active_high(TOGGLE_4_GPIO_Port, TOGGLE_4_Pin) << 3);
    return m;
}

/* Selected position pulls its line to VCC through R29 — active high. */
static uint8_t sample_rotary(void)
{
    uint8_t m = 0U;
    m |= (uint8_t)(read_active_high(ROT_SW_0_GPIO_Port, ROT_SW_0_Pin) << 0);
    m |= (uint8_t)(read_active_high(ROT_SW_1_GPIO_Port, ROT_SW_1_Pin) << 1);
    m |= (uint8_t)(read_active_high(ROT_SW_2_GPIO_Port, ROT_SW_2_Pin) << 2);
    m |= (uint8_t)(read_active_high(ROT_SW_3_GPIO_Port, ROT_SW_3_Pin) << 3);
    m |= (uint8_t)(read_active_high(ROT_SW_4_GPIO_Port, ROT_SW_4_Pin) << 4);
    m |= (uint8_t)(read_active_high(ROT_SW_5_GPIO_Port, ROT_SW_5_Pin) << 5);
    m |= (uint8_t)(read_active_high(ROT_SW_6_GPIO_Port, ROT_SW_6_Pin) << 6);
    return m;
}

/* --------------------------------------------------------------------------
 * Rotary decode
 * --------------------------------------------------------------------------
 * RS1 is a shorting (make-before-break) switch, so passing between detent n
 * and n+1 legitimately asserts BOTH lines for a moment. That is a transition,
 * not a fault: hold the last known position and say nothing.
 *
 * Everything else — no lines, two non-adjacent lines, three or more — is a
 * pattern the switch cannot physically produce, so it means a broken contact,
 * a disconnected common, or a wiring error. It is still only reported after
 * ROTARY_FAULT_MS of continuous badness, because a single sample taken at the
 * wrong instant of a fast turn can catch the switch mid-break.
 */
static uint8_t   s_rotary_pos      = ROTARY_POS_INVALID;
static uint32_t  s_rotary_bad_since = 0U;
static bool      s_rotary_bad      = false;

static uint8_t popcount8(uint8_t v)
{
    uint8_t n = 0U;
    while (v != 0U) { v &= (uint8_t)(v - 1U); n++; }
    return n;
}

static bool rotary_two_adjacent(uint8_t raw)
{
    for (uint8_t i = 0U; i + 1U < ROT_SW_COUNT; i++) {
        if (raw == (uint8_t)(3U << i)) {
            return true;
        }
    }
    return false;
}

static void rotary_decode(InputState *in, uint8_t raw, uint32_t now)
{
    const uint8_t bits = popcount8((uint8_t)(raw & 0x7FU));
    bool good = false;

    if (bits == 1U) {
        for (uint8_t i = 0U; i < ROT_SW_COUNT; i++) {
            if ((raw & (uint8_t)(1U << i)) != 0U) {
                s_rotary_pos = i;
                break;
            }
        }
        good = true;
    } else if ((bits == 2U) && rotary_two_adjacent(raw)) {
        /* Mid-detent. Keep the previous position — do not guess which of the
         * two the operator is heading for. */
        good = true;
    }

    if (good) {
        s_rotary_bad = false;
    } else if (!s_rotary_bad) {
        s_rotary_bad = true;
        s_rotary_bad_since = now;
    }

    if (s_rotary_bad && ((now - s_rotary_bad_since) >= ROTARY_FAULT_MS)) {
        in->rotary_fault = true;
        s_rotary_pos = ROTARY_POS_INVALID;
    } else if (!s_rotary_bad) {
        in->rotary_fault = false;
    }

    in->rotary_pos = s_rotary_pos;
    in->rotary_raw = (uint8_t)(raw & 0x7FU);
}

/* --------------------------------------------------------------------------
 * Encoder
 * --------------------------------------------------------------------------
 * TIM2 is 32-bit on this part but is deliberately run with ARR = 0xFFFF so
 * the counter wraps inside 16 bits. Each poll takes the 16-bit difference
 * from the previous reading — which is wrap-correct for any delta smaller
 * than half a turn of the counter — and accumulates it into a signed 32-bit
 * quadrature total. Reading a free-running counter this way means no counts
 * are lost between polls, unlike sampling the raw CNT and hoping it has not
 * wrapped.
 */
static int32_t  s_quad_accum = 0;
static uint16_t s_last_cnt   = 0U;

static void encoder_update(InputState *in)
{
    const uint16_t cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    int16_t delta = (int16_t)(cnt - s_last_cnt);   /* wrap-correct */
    s_last_cnt = cnt;

#if ENCODER_INVERT
    delta = (int16_t)(-delta);
#endif

    s_quad_accum += (int32_t)delta;

    /* Half-detent-offset floor: bins are centred on the rest positions, so
     * +-1 count of contact jitter at rest cannot flip the report and both
     * approach directions settle on the same value. See encoder_math.h for
     * the derivation and vv/unit/test_encoder_math.c for the assertions. */
    in->encoder_pos = Encoder_CountsToDetents(s_quad_accum);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void Inputs_Init(InputState *in)
{
    memset(in, 0, sizeof(*in));
    in->rotary_pos = ROTARY_POS_INVALID;

    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    s_last_cnt   = 0U;
    s_quad_accum = 0;

    /* Seed the debouncers from the pins as they are right now, otherwise the
     * first poll reports every currently-held button as a fresh press. */
    debounce_seed(&s_buttons,     sample_buttons());
    debounce_seed(&s_buttons_int, sample_buttons_int());
    debounce_seed(&s_toggles,     sample_toggles());
    debounce_seed(&s_enc_button,
                  read_active_low(ENC_BUT_GPIO_Port, ENC_BUT_Pin));

    in->button_mask     = s_buttons.stable;
    in->button_int_mask = s_buttons_int.stable;
    in->toggle_mask     = s_toggles.stable;
    in->encoder_button  = s_enc_button.stable;
}

void Inputs_Poll(InputState *in)
{
    const uint32_t now = HAL_GetTick();

    encoder_update(in);

    in->encoder_button = debounce_update(
        &s_enc_button,
        read_active_low(ENC_BUT_GPIO_Port, ENC_BUT_Pin),
        1U, now, BUTTON_DEBOUNCE_MS);

    in->button_mask = debounce_update(&s_buttons, sample_buttons(),
                                      BUTTON_COUNT, now, BUTTON_DEBOUNCE_MS);
    in->button_int_mask = debounce_update(&s_buttons_int, sample_buttons_int(),
                                          BUTTON_INT_COUNT, now,
                                          BUTTON_DEBOUNCE_MS);
    in->toggle_mask = debounce_update(&s_toggles, sample_toggles(),
                                      TOGGLE_COUNT, now, BUTTON_DEBOUNCE_MS);

    rotary_decode(in, sample_rotary(), now);
}

void Inputs_SetEncoder(InputState *in, int16_t pos)
{
    /* Reset both halves together so the accumulated total and the hardware
     * counter cannot disagree afterwards. */
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    s_last_cnt   = 0U;
    s_quad_accum = Encoder_DetentsToCounts(pos);   /* bin centre of `pos` */
    in->encoder_pos = pos;
}
