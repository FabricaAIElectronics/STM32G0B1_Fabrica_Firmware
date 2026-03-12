#include "ESTOP.h"
#include "main.h"
#include "stm32g0xx_hal_gpio.h"
#include <stdint.h>



uint8_t estop_flag;

/*🔴Preserve*/
void ESTOP_Init(void)
{
    estop_flag = 0;
    HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_RESET); /* de-assert */
}

int ESTOP_State(void)
{
    return estop_flag;
}

/*🔴Preserve*/
int ESTOP_Check(void)
{
    GPIO_PinState NC = HAL_GPIO_ReadPin(EStop_NC_INT_GPIO_Port, EStop_NC_INT_Pin);
    GPIO_PinState NO = HAL_GPIO_ReadPin(EStop_NO_INT_GPIO_Port, EStop_NO_INT_Pin);
    if (NC == GPIO_PIN_SET && NO == GPIO_PIN_RESET) return ESTOP_OK;
    if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET) return ESTOP_TRIGGERED;
    return ESTOP_FAULT;
}

/*🔴Preserve*/
void ESTOP_State_Machine(void)
{
    int check = ESTOP_Check();
    static int last_check = ESTOP_OK;
    static int debounced_state = ESTOP_OK;

    const uint32_t DEBOUNCE_MS = 20;
    static uint32_t last_change_ts = 0;
    uint32_t now = HAL_GetTick();

    if (check != last_check) {
        last_check = check;
        last_change_ts = now;
        return;
    }

    if ((now - last_change_ts) < DEBOUNCE_MS) {
        return; /* not stable yet */
    }

    if (debounced_state != last_check)
        debounced_state = last_check;


    switch (check)
    {
        case ESTOP_OK: // OK
            estop_flag =0;
            HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_RESET);
            break;
        
        case ESTOP_TRIGGERED: // Triggered
            estop_flag =1;
            HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_SET);
            break;

        case ESTOP_FAULT: // Fault

            break; //implement blink if needed

    }
}

