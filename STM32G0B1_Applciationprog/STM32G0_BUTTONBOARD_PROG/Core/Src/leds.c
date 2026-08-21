/**
  ******************************************************************************
  * @file    leds.c
  * @brief   LED output and buffer-bank arbitration.
  ******************************************************************************
  */

#include "leds.h"
#include "board_pins.h"

/* Both output-enables are ACTIVE LOW. */
#define BUFF_ENABLE     GPIO_PIN_RESET
#define BUFF_DISABLE    GPIO_PIN_SET

/* The STM32 path inverts once through the SN74ACT240, so a lit LED is a LOW
 * pin. Naming the two levels avoids getting this backwards in the middle of
 * a bit loop. */
#define LED_LIT_LEVEL   GPIO_PIN_RESET
#define LED_DARK_LEVEL  GPIO_PIN_SET

static LedSource s_source      = LED_SRC_DIRECT;
static uint8_t   s_led_mask    = 0U;
static uint8_t   s_led_int_mask = 0U;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} LedPin;

static const LedPin k_led[LED_COUNT] = {
    { LED_1_GPIO_Port, LED_1_Pin },
    { LED_2_GPIO_Port, LED_2_Pin },
    { LED_3_GPIO_Port, LED_3_Pin },
    { LED_4_GPIO_Port, LED_4_Pin },
    { LED_5_GPIO_Port, LED_5_Pin },
    { LED_6_GPIO_Port, LED_6_Pin },
};

static const LedPin k_led_int[LED_INT_COUNT] = {
    { LED_INT_1_GPIO_Port, LED_INT_1_Pin },
    { LED_INT_2_GPIO_Port, LED_INT_2_Pin },
    { LED_INT_3_GPIO_Port, LED_INT_3_Pin },
    { LED_INT_4_GPIO_Port, LED_INT_4_Pin },
};

static void apply_masks(void)
{
    for (uint8_t i = 0U; i < LED_COUNT; i++) {
        const GPIO_PinState lvl = ((s_led_mask & (uint8_t)(1U << i)) != 0U)
                                ? LED_LIT_LEVEL : LED_DARK_LEVEL;
        HAL_GPIO_WritePin(k_led[i].port, k_led[i].pin, lvl);
    }
    for (uint8_t i = 0U; i < LED_INT_COUNT; i++) {
        const GPIO_PinState lvl = ((s_led_int_mask & (uint8_t)(1U << i)) != 0U)
                                ? LED_LIT_LEVEL : LED_DARK_LEVEL;
        HAL_GPIO_WritePin(k_led_int[i].port, k_led_int[i].pin, lvl);
    }
}

void Leds_Init(void)
{
    /* Order matters. Park the data lines dark first, then hand the nets to
     * the direct path. Doing it the other way round would briefly enable a
     * bank whose inputs have not been driven yet.
     *
     * Both enables are driven push-pull from here on. That also keeps PC0/PC1
     * out of Hi-Z, which matters because R51/R52 pull them toward +5 V
     * through 100 k — a 3.3 V pin left floating under a 5 V pull-up is only
     * safe if the pin is five-volt tolerant, and driving it removes the
     * question entirely. */
    s_led_mask     = 0U;
    s_led_int_mask = 0U;
    apply_masks();

    HAL_GPIO_WritePin(BUFF_SEL_2_GPIO_Port, BUFF_SEL_2_Pin, BUFF_DISABLE);
    HAL_GPIO_WritePin(BUFF_SEL_1_GPIO_Port, BUFF_SEL_1_Pin, BUFF_ENABLE);
    s_source = LED_SRC_DIRECT;
}

void Leds_SelectSource(LedSource src)
{
    if (src == s_source) {
        return;
    }

    if (src == LED_SRC_STM32) {
        HAL_GPIO_WritePin(BUFF_SEL_1_GPIO_Port, BUFF_SEL_1_Pin, BUFF_DISABLE);
        HAL_GPIO_WritePin(BUFF_SEL_2_GPIO_Port, BUFF_SEL_2_Pin, BUFF_ENABLE);
    } else {
        HAL_GPIO_WritePin(BUFF_SEL_2_GPIO_Port, BUFF_SEL_2_Pin, BUFF_DISABLE);
        HAL_GPIO_WritePin(BUFF_SEL_1_GPIO_Port, BUFF_SEL_1_Pin, BUFF_ENABLE);
    }
    s_source = src;
}

LedSource Leds_GetSource(void)
{
    return s_source;
}

void Leds_Set(uint8_t led_mask, uint8_t led_int_mask)
{
    s_led_mask     = (uint8_t)(led_mask     & 0x3FU);
    s_led_int_mask = (uint8_t)(led_int_mask & 0x0FU);
    apply_masks();
}

uint8_t Leds_GetMask(void)     { return s_led_mask; }
uint8_t Leds_GetIntMask(void)  { return s_led_int_mask; }
