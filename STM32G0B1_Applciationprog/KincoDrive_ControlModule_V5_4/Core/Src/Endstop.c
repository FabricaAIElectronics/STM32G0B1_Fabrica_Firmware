#include "Endstop.h"
#include "ESTOP.h"
#include "main.h"
#include "stm32g0xx_hal_gpio.h"
#include <stdint.h>
#include <string.h>

static uint8_t endstop_flag[NUM_ENDSTOPS];

void Endstop_Init(void)
{
    for (int i = 0; i < NUM_ENDSTOPS; i++) {
        endstop_flag[i] = 0;
    }
}

/*🔴Preserve*/
int Endstop_Check(Endstop_Module_t endstop)
{
    GPIO_PinState NC;
    GPIO_PinState NO;
    GPIO_PinState state;
    switch (endstop)
    {
        case ENDSTOP_EXTRUDER_HEIGHT_TOP:
            NC = HAL_GPIO_ReadPin(ENDSTOP_EH_H_NC_INT_GPIO_Port, ENDSTOP_EH_H_NC_INT_Pin);
            NO = HAL_GPIO_ReadPin(ENDSTOP_EH_H_NO_INT_GPIO_Port, ENDSTOP_EH_H_NO_INT_Pin);
            if (NC == GPIO_PIN_SET && NO == GPIO_PIN_RESET) return ENDSTOP_OK;
            if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET) return ENDSTOP_TRIGGERED;
            return ENDSTOP_FAULT;
        case ENDSTOP_EXTRUDER_HEIGHT_BOTTOM:
            state = HAL_GPIO_ReadPin(ENDSTOP_EH_L_NO_INT_GPIO_Port, ENDSTOP_EH_L_NO_INT_Pin);
            if (state == GPIO_PIN_SET) return ENDSTOP_TRIGGERED;
            return ENDSTOP_OK;
        case ENDSTOP_EXTRUDER_MOBILE_TOP:
            NC = HAL_GPIO_ReadPin(ENDSTOP_EP_H_NC_INT_GPIO_Port, ENDSTOP_EP_H_NC_INT_Pin);
            NO = HAL_GPIO_ReadPin(ENDSTOP_EP_H_NO_INT_GPIO_Port, ENDSTOP_EP_H_NO_INT_Pin);
            if (NC == GPIO_PIN_SET && NO == GPIO_PIN_RESET) return ENDSTOP_OK;
            if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET) return ENDSTOP_TRIGGERED;
            return ENDSTOP_FAULT;
        case ENDSTOP_EXTRUDER_MOBILE_BOTTOM:
            NC = HAL_GPIO_ReadPin(ENDSTOP_EP_L_NC_INT_GPIO_Port, ENDSTOP_EP_L_NC_INT_Pin);
            NO = HAL_GPIO_ReadPin(ENDSTOP_EP_L_NO_INT_GPIO_Port, ENDSTOP_EP_L_NO_INT_Pin);
            if (NC == GPIO_PIN_SET && NO == GPIO_PIN_RESET) return ENDSTOP_OK;
            if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET) return ENDSTOP_TRIGGERED;
            return ENDSTOP_FAULT;
        case ENDSTOP_SCRUBBING_FRONT_TOP:
            NC = HAL_GPIO_ReadPin(ENDSTOP_SC_H_NC_INT_GPIO_Port, ENDSTOP_SC_H_NC_INT_Pin);
            NO = HAL_GPIO_ReadPin(ENDSTOP_SC_H_NO_INT_GPIO_Port, ENDSTOP_SC_H_NO_INT_Pin);
            if (NC == GPIO_PIN_SET && NO == GPIO_PIN_RESET) return ENDSTOP_OK;
            if (NC == GPIO_PIN_RESET && NO == GPIO_PIN_SET) return ENDSTOP_TRIGGERED;
            return ENDSTOP_FAULT;
        default:
            return PARAM_ERROR; // Invalid endstop
    }

}

size_t CAN_Packer_Endstop_and_ESTOP_2Byte(uint8_t *out1, uint8_t *out2, size_t out_size)
{
    if (out1 == NULL || out2 == NULL || out_size < 2) return 0;

    uint8_t triggered = 0;
    uint8_t fault     = 0;

    /* Endstops: bits 0–4 */
    for (int i = 0; i < NUM_ENDSTOPS; ++i) {
        int state = Endstop_Check((Endstop_Module_t)i);

        if (state == ENDSTOP_TRIGGERED) {
            triggered |= (1U << i);
        } else if (state == ENDSTOP_FAULT) {
            fault |= (1U << i);
            /* fault is treated as NOT triggered, so triggered bit stays 0 */
        }
        /* ENDSTOP_OK: both bits stay 0 */
    }

    /* ESTOP: bit 5 */
    int estop_state = ESTOP_Check();
    if (estop_state == ENDSTOP_TRIGGERED) {
        triggered |= (1U << 5);
    } else if (estop_state == ENDSTOP_FAULT) {
        fault |= (1U << 5);
    }

    *out1 = triggered;
    *out2 = fault;
    return 2;
}