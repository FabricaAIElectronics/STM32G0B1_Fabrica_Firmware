/**
  ******************************************************************************
  * @file    leds.h
  * @brief   LED output and buffer-bank arbitration for the button board V5.5.
  *
  * The board can drive its LEDs from two places, wire-OR'd onto the same nets:
  *
  *   LED_SRC_DIRECT  the J1 / direct-feedback path (U3A/U3B/U5A), enabled by
  *                   BUFF_SEL_1 low. This is the power-on default, matching
  *                   the intent behind fitting R51 as a pull-down.
  *
  *   LED_SRC_STM32   this MCU (U5B/U7A/U7B), enabled by BUFF_SEL_2 low.
  *
  * BUFF_SEL_1 and BUFF_SEL_2 are independent GPIOs, NOT complements — pulling
  * both low puts two buffer banks onto LED_1, LED_2 and LED_INT_1..4 at the
  * same time, which is an output fight, not a wired-OR. Leds_SelectSource()
  * is the only code permitted to touch either pin and it always disables the
  * outgoing bank before enabling the incoming one.
  ******************************************************************************
  */

#ifndef INC_LEDS_H_
#define INC_LEDS_H_

#include "stm32g0xx_hal.h"

typedef enum {
    LED_SRC_DIRECT = 0,     /* J1 / direct feedback owns the LED nets        */
    LED_SRC_STM32  = 1      /* this MCU owns the LED nets                    */
} LedSource;

/* Park every LED data line in the dark state and select LED_SRC_DIRECT.
 * Call this before anything else in MX_GPIO_Init(): BUFF_SEL_1 has no
 * hardware default (R51 is DNP) and the ACT240 data inputs float while the
 * MCU is in reset, so until this runs the LED banks are in an undefined
 * state and may both be driving. */
void Leds_Init(void);

/* Swap which bank drives the LED nets. Disabling first costs one extra GPIO
 * write and guarantees the two banks are never simultaneously enabled. */
void Leds_SelectSource(LedSource src);

LedSource Leds_GetSource(void);

/* Apply a logical LED pattern. Bit set = LED lit.
 *   led_mask     bits 0..5 -> LED_1 .. LED_6
 *   led_int_mask bits 0..3 -> LED_INT_1 .. LED_INT_4
 *
 * The pins are driven INVERTED, because the STM32 path goes through a single
 * inverting SN74ACT240 stage: a lit LED means the GPIO is driven LOW. (The
 * J1 path inverts twice and so is non-inverting — the two sources genuinely
 * disagree on polarity; see the schematic review.)
 *
 * Writes are applied whatever the current source is. That is deliberate: the
 * ACT240 is disabled when BUFF_SEL_2 is high, so the data lines are harmless,
 * and the pattern is then already correct the instant the host switches
 * ownership to LED_SRC_STM32. */
void Leds_Set(uint8_t led_mask, uint8_t led_int_mask);

uint8_t Leds_GetMask(void);
uint8_t Leds_GetIntMask(void);

#endif /* INC_LEDS_H_ */
