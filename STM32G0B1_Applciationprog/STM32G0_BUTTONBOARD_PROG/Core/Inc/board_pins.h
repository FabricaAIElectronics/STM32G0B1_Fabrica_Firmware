/**
  ******************************************************************************
  * @file    board_pins.h
  * @brief   fabricaAI calibration knob V5.5 — single source of truth for the
  *          STM32G0B1RET6 pin assignment.
  *
  * Every pin below was read off sheet 2 (`stm32G0B1.SchDoc`) of the V5.5
  * schematic and cross-checked against the G0B1 alternate-function table.
  * Nothing else in this project may hard-code a port/pin pair — if the board
  * is respun, this file is the only one that changes.
  *
  * ---------------------------------------------------------------------------
  * Board wiring notes that the firmware has to honour
  * ---------------------------------------------------------------------------
  *
  * 1. LED polarity is INVERTED on the STM32 path.
  *    The STM32 drives the SN74ACT240 buffer inputs directly (U6/U8 were
  *    deleted in rev 2). The ACT240 is an inverting buffer, so:
  *
  *        BUTTON_n_uC = 0  ->  LED_n lit
  *        BUTTON_n_uC = 1  ->  LED_n dark
  *
  *    The J1 / direct-feedback path is non-inverting (U2 LVC2G04 + ACT240
  *    cancel), which is why the two sources disagree on polarity. See
  *    `claude/schematic-review-V5_5.md` section 3.
  *
  * 2. BUFF_SEL_1 and BUFF_SEL_2 are INDEPENDENT active-low output enables,
  *    not complements. Driving both low puts two buffer banks on the same
  *    LED nets at once. Leds_SelectSource() is the only function allowed to
  *    touch them and it guarantees mutual exclusion.
  *
  * 3. BUFF_SEL_1 has no default state (R51 is DNP) and the ACT240 data
  *    inputs float while the MCU is in reset. Both are open hardware items.
  *    Firmware compensates by driving BUFF_SEL_1/2 and all ten LED lines to
  *    a defined level as the very first thing MX_GPIO_Init() does.
  *
  * 4. ENC_BUT is active low: R37 10 k pull-up to VCC, R36 100 R to GND
  *    through the switch. Use GPIO_NOPULL — the board already pulls it.
  *
  * 5. PA14 is SWCLK *and* BOOT0. The nBOOT_SEL option byte must be
  *    programmed so a debugger holding SWCLK cannot drop the part into the
  *    ST system bootloader, which would bypass OpenBLT entirely.
  ******************************************************************************
  */

#ifndef INC_BOARD_PINS_H_
#define INC_BOARD_PINS_H_

#include "stm32g0xx_hal.h"

/* ==========================================================================
 * CAN — FDCAN1, TJA1057GT/3J transceiver
 * ========================================================================== */
#define CAN_RX_Pin              GPIO_PIN_8      /* PB8  AF3 FDCAN1_RX        */
#define CAN_RX_GPIO_Port        GPIOB
#define CAN_TX_Pin              GPIO_PIN_9      /* PB9  AF3 FDCAN1_TX        */
#define CAN_TX_GPIO_Port        GPIOB

/* ==========================================================================
 * I2C1 — AT24C256 EEPROM (IC1) and the external header P1
 * ========================================================================== */
#define I2C_SCL_Pin             GPIO_PIN_6      /* PB6  AF6 I2C1_SCL         */
#define I2C_SCL_GPIO_Port       GPIOB
#define I2C_SDA_Pin             GPIO_PIN_7      /* PB7  AF6 I2C1_SDA         */
#define I2C_SDA_GPIO_Port       GPIOB

/* ==========================================================================
 * PEC11L incremental encoder — TIM2 encoder mode, x4 decoding
 *
 * PA0/PA1 are TIM2_CH1/TIM2_CH2 on AF2, so the quadrature decode runs in
 * hardware. TIM2 is a 32-bit timer on this part; the counter is deliberately
 * clamped to 16 bits (ARR = 0xFFFF) so the wrap arithmetic in Inputs_Encoder
 * stays in one word.
 * ========================================================================== */
#define ENC_A_Pin               GPIO_PIN_0      /* PA0  AF2 TIM2_CH1         */
#define ENC_A_GPIO_Port         GPIOA
#define ENC_B_Pin               GPIO_PIN_1      /* PA1  AF2 TIM2_CH2         */
#define ENC_B_GPIO_Port         GPIOA
#define ENC_BUT_Pin             GPIO_PIN_2      /* PA2  active low, ext 10k  */
#define ENC_BUT_GPIO_Port       GPIOA

/* ==========================================================================
 * ALPS SRBV170501 — 7-position rotary switch, one-hot
 *
 * Seven separate lines, each through 100 R (RN7/RN9). Exactly one should be
 * asserted at a time; Inputs_Rotary() treats "none" and "more than one" as
 * invalid rather than guessing, because whether the part is shorting or
 * non-shorting is still an open item on the schematic checklist.
 * ========================================================================== */
#define ROT_SW_COUNT            7U
#define ROT_SW_0_Pin            GPIO_PIN_5      /* PC5                       */
#define ROT_SW_0_GPIO_Port      GPIOC
#define ROT_SW_1_Pin            GPIO_PIN_0      /* PB0                       */
#define ROT_SW_1_GPIO_Port      GPIOB
#define ROT_SW_2_Pin            GPIO_PIN_1      /* PB1                       */
#define ROT_SW_2_GPIO_Port      GPIOB
#define ROT_SW_3_Pin            GPIO_PIN_2      /* PB2                       */
#define ROT_SW_3_GPIO_Port      GPIOB
#define ROT_SW_4_Pin            GPIO_PIN_10     /* PB10                      */
#define ROT_SW_4_GPIO_Port      GPIOB
#define ROT_SW_5_Pin            GPIO_PIN_11     /* PB11                      */
#define ROT_SW_5_GPIO_Port      GPIOB
#define ROT_SW_6_Pin            GPIO_PIN_12     /* PB12                      */
#define ROT_SW_6_GPIO_Port      GPIOB

/* ==========================================================================
 * C&K SPDT toggle switches SW1..SW4
 *
 * Note the scrambled designator mapping on the schematic — SW1->SW_1_3V3,
 * SW3->SW_2_3V3, SW2->SW_3_3V3, SW4->SW_4_3V3. The NET names are what the
 * firmware indexes by, so TOGGLE_1 below is net SW_1_3V3 regardless of which
 * physical switch body carries that silkscreen.
 *
 * Each net is 100 R to VCC (closed) or 100 R to GND (open) after the rev-2
 * change from 10 k, so both levels are hard-driven. Use GPIO_NOPULL.
 * ========================================================================== */
#define TOGGLE_COUNT            4U
#define TOGGLE_1_Pin            GPIO_PIN_11     /* PA11  SW_1_3V3            */
#define TOGGLE_1_GPIO_Port      GPIOA
#define TOGGLE_2_Pin            GPIO_PIN_12     /* PA12  SW_2_3V3            */
#define TOGGLE_2_GPIO_Port      GPIOA
#define TOGGLE_3_Pin            GPIO_PIN_15     /* PA15  SW_3_3V3            */
#define TOGGLE_3_GPIO_Port      GPIOA
#define TOGGLE_4_Pin            GPIO_PIN_8      /* PC8   SW_4_3V3            */
#define TOGGLE_4_GPIO_Port      GPIOC

/* ==========================================================================
 * Button inputs — 5 V contacts translated to 3.3 V by LSF0108 (U1/U4)
 *
 * Buttons 1..6 come from P2/P3 and J1; INT_1..4 are the internal panel
 * buttons. All are pulled up to +5 V by RN1/RN2 on the far side of the
 * translator, so a pressed button reads LOW. Use GPIO_NOPULL — an internal
 * pull-up would fight the translator.
 * ========================================================================== */
#define BUTTON_COUNT            6U
#define BUTTON_INT_COUNT        4U

#define BUTTON_1_Pin            GPIO_PIN_13     /* PB13  Button_3V3_1        */
#define BUTTON_1_GPIO_Port      GPIOB
#define BUTTON_2_Pin            GPIO_PIN_14     /* PB14  Button_3V3_2        */
#define BUTTON_2_GPIO_Port      GPIOB
#define BUTTON_3_Pin            GPIO_PIN_15     /* PB15  Button_3V3_3        */
#define BUTTON_3_GPIO_Port      GPIOB
#define BUTTON_4_Pin            GPIO_PIN_8      /* PA8   Button_3V3_4        */
#define BUTTON_4_GPIO_Port      GPIOA
#define BUTTON_5_Pin            GPIO_PIN_9      /* PA9   Button_3V3_5        */
#define BUTTON_5_GPIO_Port      GPIOA
#define BUTTON_6_Pin            GPIO_PIN_6      /* PC6   Button_3V3_6        */
#define BUTTON_6_GPIO_Port      GPIOC

#define BUTTON_INT_1_Pin        GPIO_PIN_7      /* PC7   Button_3V3_int_1    */
#define BUTTON_INT_1_GPIO_Port  GPIOC
#define BUTTON_INT_2_Pin        GPIO_PIN_8      /* PD8   Button_3V3_int_2    */
#define BUTTON_INT_2_GPIO_Port  GPIOD
#define BUTTON_INT_3_Pin        GPIO_PIN_9      /* PD9   Button_3V3_int_3    */
#define BUTTON_INT_3_GPIO_Port  GPIOD
#define BUTTON_INT_4_Pin        GPIO_PIN_10     /* PA10  Button_3V3_int_4    */
#define BUTTON_INT_4_GPIO_Port  GPIOA

/* ==========================================================================
 * LED outputs — straight into the SN74ACT240 inverting buffers
 *
 * Schematic port names are LED_CTRL_n but the NETS are BUTTON_n_uC; the
 * names below follow the nets, which is what you will see on the board.
 * Remember: driving LOW lights the LED.
 * ========================================================================== */
#define LED_COUNT               6U
#define LED_INT_COUNT           4U

#define LED_1_Pin               GPIO_PIN_9      /* PC9   BUTTON_1_uC         */
#define LED_1_GPIO_Port         GPIOC
#define LED_2_Pin               GPIO_PIN_0      /* PD0   BUTTON_2_uC         */
#define LED_2_GPIO_Port         GPIOD
#define LED_3_Pin               GPIO_PIN_1      /* PD1   BUTTON_3_uC         */
#define LED_3_GPIO_Port         GPIOD
#define LED_4_Pin               GPIO_PIN_2      /* PD2   BUTTON_4_uC         */
#define LED_4_GPIO_Port         GPIOD
#define LED_5_Pin               GPIO_PIN_3      /* PD3   BUTTON_5_uC         */
#define LED_5_GPIO_Port         GPIOD
#define LED_6_Pin               GPIO_PIN_4      /* PD4   BUTTON_6_uC         */
#define LED_6_GPIO_Port         GPIOD

#define LED_INT_1_Pin           GPIO_PIN_5      /* PD5   BUTTON_INT_1_uC     */
#define LED_INT_1_GPIO_Port     GPIOD
#define LED_INT_2_Pin           GPIO_PIN_6      /* PD6   BUTTON_INT_2_uC     */
#define LED_INT_2_GPIO_Port     GPIOD
#define LED_INT_3_Pin           GPIO_PIN_3      /* PB3   BUTTON_INT_3_uC     */
#define LED_INT_3_GPIO_Port     GPIOB
#define LED_INT_4_Pin           GPIO_PIN_4      /* PB4   BUTTON_INT_4_uC     */
#define LED_INT_4_GPIO_Port     GPIOB

/* ==========================================================================
 * Buffer bank output-enables — both ACTIVE LOW
 *
 * BUFF_SEL_1 low  -> U3A/U3B/U5A enabled: J1 / direct feedback drives LEDs
 * BUFF_SEL_2 low  -> U5B/U7A/U7B enabled: the STM32 drives LEDs
 *
 * Never both low. See Leds_SelectSource().
 * ========================================================================== */
#define BUFF_SEL_1_Pin          GPIO_PIN_0      /* PC0                       */
#define BUFF_SEL_1_GPIO_Port    GPIOC
#define BUFF_SEL_2_Pin          GPIO_PIN_1      /* PC1                       */
#define BUFF_SEL_2_GPIO_Port    GPIOC

/* ==========================================================================
 * Board housekeeping
 * ========================================================================== */
#define LED_OUT_Pin             GPIO_PIN_5      /* PA5  green heartbeat LED  */
#define LED_OUT_GPIO_Port       GPIOA
#define BLUE_BUTTON_Pin         GPIO_PIN_13     /* PC13 on-board user button */
#define BLUE_BUTTON_GPIO_Port   GPIOC

/* Debug: PA13 = SWDIO, PA14 = SWCLK/BOOT0. Left at their reset AF — do not
 * reconfigure them as GPIO or the board becomes unflashable over SWD. */

#endif /* INC_BOARD_PINS_H_ */
