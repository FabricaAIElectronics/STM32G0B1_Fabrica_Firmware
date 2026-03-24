/**
 * @file    ESTOP.c
 * @brief   Emergency stop switch reading and debounced state machine.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#include "ESTOP.h"
#include "main.h"
#include "stm32g0xx_hal_gpio.h"
#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Internal state
 * ════════════════════════════════════════════════════════════════════════════ */

static uint8_t estop_flag = 0;

/* ════════════════════════════════════════════════════════════════════════════
 *  Initialization
 * ════════════════════════════════════════════════════════════════════════════ */

void ESTOP_Init(void)
{
    estop_flag = 0;
    HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_RESET);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Raw switch reading (NC + NO cross-check)
 * ════════════════════════════════════════════════════════════════════════════ */

int ESTOP_Check(void)
{
    GPIO_PinState NC = HAL_GPIO_ReadPin(EStop_NC_INT_GPIO_Port, EStop_NC_INT_Pin);
    GPIO_PinState NO = HAL_GPIO_ReadPin(EStop_NO_INT_GPIO_Port, EStop_NO_INT_Pin);

    if (NC == GPIO_PIN_SET   && NO == GPIO_PIN_RESET) return ESTOP_OK;
    if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET)   return ESTOP_TRIGGERED;
    return ESTOP_FAULT;
}

int ESTOP_State(void)
{
    return estop_flag;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Debounced state machine — call periodically from main loop
 * ════════════════════════════════════════════════════════════════════════════ */

void ESTOP_State_Machine(void)
{
    static int      last_raw        = ESTOP_OK;
    static int      debounced_state = ESTOP_OK;
    static uint32_t last_change_ts  = 0;

    const uint32_t DEBOUNCE_MS = 20;

    int raw = ESTOP_Check();
    uint32_t now = HAL_GetTick();

    /* Reset debounce timer on any state change */
    if (raw != last_raw) {
        last_raw = raw;
        last_change_ts = now;
        return;
    }

    /* Wait for stable reading */
    if ((now - last_change_ts) < DEBOUNCE_MS) {
        return;
    }

    /* Latch the debounced state */
    if (debounced_state != raw) {
        debounced_state = raw;
    }

    /* Apply output */
    switch (debounced_state) {
    case ESTOP_OK:
        estop_flag = 0;
        HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_RESET);
        break;

    case ESTOP_TRIGGERED:
        estop_flag = 1;
        HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_SET);
        break;

    case ESTOP_FAULT:
        /* TODO: implement LED blink for fault indication if needed */
        break;
    }
}
