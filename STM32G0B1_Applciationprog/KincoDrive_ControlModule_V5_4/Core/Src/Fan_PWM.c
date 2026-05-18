/**
 * @file    Fan_PWM.c
 * @brief   Fan PWM speed control and tachometer speed measurement.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#include "Fan_PWM.h"
#include "main.h"
#include <stdint.h>

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim14;

/* ════════════════════════════════════════════════════════════════════════════
 *  PWM pin mapping
 * ════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t           channel;
} FanPwmPin_t;

/* Shadow array: last PWM setpoint written to each fan (0–100 %) */
static uint8_t fan_speed_pct[5] = {0, 0, 0, 0, 0};

static FanPwmPin_t fan_pwm[5] = {
    { .timer = &htim1,  .channel = TIM_CHANNEL_1 },  /* FAN_DR */
    { .timer = &htim1,  .channel = TIM_CHANNEL_2 },  /* FAN_EP */
    { .timer = &htim1,  .channel = TIM_CHANNEL_3 },  /* FAN_EH */
    { .timer = &htim1,  .channel = TIM_CHANNEL_4 },  /* FAN_ST */
    { .timer = &htim14, .channel = TIM_CHANNEL_1 },  /* FAN_SF */
};

/* ════════════════════════════════════════════════════════════════════════════
 *  Tachometer pin mapping and DMA capture buffers
 * ════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t           channel;
} FanTachoPin_t;

static FanTachoPin_t fan_tacho[5] = {
    { .timer = &htim2, .channel = TIM_CHANNEL_1 },   /* FAN_DR */
    { .timer = &htim2, .channel = TIM_CHANNEL_2 },   /* FAN_EP */
    { .timer = &htim2, .channel = TIM_CHANNEL_3 },   /* FAN_EH */
    { .timer = &htim2, .channel = TIM_CHANNEL_4 },   /* FAN_ST */
    { .timer = &htim3, .channel = TIM_CHANNEL_2 },   /* FAN_SF */
};

#define TACHO_DMA_DEPTH     5

static uint16_t tacho_buf[5][TACHO_DMA_DEPTH];

/* ════════════════════════════════════════════════════════════════════════════
 *  PWM speed control
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Get the internal array index for a fan number (1-based → 0-based).
 * @retval Index (0–4) on success, -1 if invalid.
 */
static int fan_index(FanNumber_t fan_number)
{
    int idx = (int)fan_number - 1;
    return (idx >= 0 && idx < 5) ? idx : -1;
}

void set_Fan_PWM(FanNumber_t fan_number, uint8_t speed_percent)
{
    int idx = fan_index(fan_number);
    if (idx < 0) return;

    FanPwmPin_t *fan = &fan_pwm[idx];
    if (fan->timer == NULL) return;

    if (speed_percent > 100U) {
        speed_percent = 100U;
    }

    uint32_t arr   = fan->timer->Instance->ARR;
    uint32_t pulse = (arr * (uint32_t)speed_percent) / 100U;

    __HAL_TIM_SET_COMPARE(fan->timer, fan->channel, pulse);

    /* Update shadow array so get_Fan_PWM_Pct() can read it back */
    fan_speed_pct[idx] = speed_percent;
}

uint8_t get_Fan_PWM_Pct(FanNumber_t fan_number)
{
    int idx = fan_index(fan_number);
    if (idx < 0) return 0U;
    return fan_speed_pct[idx];
}

void start_all_Fan_PWM(void)
{
    for (int i = 0; i < 5; ++i) {
        if (fan_pwm[i].timer != NULL) {
            HAL_TIM_PWM_Start(fan_pwm[i].timer, fan_pwm[i].channel);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Tachometer DMA and speed calculation
 * ════════════════════════════════════════════════════════════════════════════ */

void start_Fan_Tacho_DMA(void)
{
    for (int i = 0; i < 5; ++i) {
        /* Guard: skip if timer has no DMA handle linked for this channel.
         * Without DMA configured in the MSP, the hdma[] pointers are NULL
         * and HAL_TIM_IC_Start_DMA would dereference them → undefined behaviour.
         * Fall back to interrupt-based capture instead. */
        uint32_t ch  = fan_tacho[i].channel;
        uint32_t idx = (ch == TIM_CHANNEL_1) ? TIM_DMA_ID_CC1 :
                       (ch == TIM_CHANNEL_2) ? TIM_DMA_ID_CC2 :
                       (ch == TIM_CHANNEL_3) ? TIM_DMA_ID_CC3 :
                                               TIM_DMA_ID_CC4;
        if (fan_tacho[i].timer->hdma[idx] != NULL) {
            HAL_TIM_IC_Start_DMA(fan_tacho[i].timer, ch,
                                 (uint32_t *)tacho_buf[i], TACHO_DMA_DEPTH);
        } else {
            HAL_TIM_IC_Start_IT(fan_tacho[i].timer, ch);
        }
    }
}

void Fan_Tacho_Speed_Calculate(FanNumber_t fan_number, uint16_t *speed_pct)
{
    int idx = fan_index(fan_number);
    if (idx < 0) {
        *speed_pct = 0;
        return;
    }

    /* Period = difference between two consecutive DMA captures */
    uint16_t ticks = tacho_buf[idx][1] - tacho_buf[idx][0];

    if (ticks == 0) {
        *speed_pct = 0;    /* fan stopped or no signal */
        return;
    }

    /* Fixed max RPM for % calculation */
    const uint16_t fan_max_rpm = 5000U;

    /*
     * Timer clock = 800 kHz (e.g. 8 MHz / PSC=9).
     * frequency_hz = 800000 / ticks
     * RPM = frequency_hz * 30   (assuming 2 pulses per revolution)
     */
    uint32_t frequency_hz = 800000U / (uint32_t)ticks;
    uint32_t rpm = frequency_hz * 30U;

    uint32_t pct = (rpm * 100U) / (uint32_t)fan_max_rpm;
    if (pct > 100U) pct = 100U;

    *speed_pct = (uint16_t)pct;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN telemetry packer
 * ════════════════════════════════════════════════════════════════════════════ */

size_t CAN_Packer_Fan_Speed_1Byte(FanNumber_t fan_number, uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint16_t speed_pct = 0;
    Fan_Tacho_Speed_Calculate(fan_number, &speed_pct);

    out[0] = (uint8_t)speed_pct;
    return 1;
}
