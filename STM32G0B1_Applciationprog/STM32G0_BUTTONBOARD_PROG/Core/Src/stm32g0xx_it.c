/**
  ******************************************************************************
  * @file    stm32g0xx_it.c
  * @brief   Interrupt service routines.
  ******************************************************************************
  */

#include "main.h"

extern FDCAN_HandleTypeDef hfdcan1;

/******************************************************************************/
/*           Cortex-M0+ processor exceptions                                  */
/******************************************************************************/

void NMI_Handler(void)
{
    /* Reached when the clock security system fires — the 8 MHz crystal has
     * stopped and the PLL has been switched to HSI. Clear the flag and carry
     * on: the board keeps running at a slightly different clock, and CAN will
     * report the resulting bit-timing error rather than the part just dying
     * here. */
    if (__HAL_RCC_GET_IT(RCC_IT_CSS)) {
        __HAL_RCC_CLEAR_IT(RCC_IT_CSS);
    }
}

void HardFault_Handler(void)
{
    while (1) { }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/******************************************************************************/
/*                 STM32G0xx peripheral interrupt handlers                    */
/******************************************************************************/

/* FDCAN1 interrupt line 0 shares its vector with TIM16 on this part. TIM16 is
 * unused here, so the handler goes straight to the FDCAN driver. */
void TIM16_FDCAN_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&hfdcan1);
}
