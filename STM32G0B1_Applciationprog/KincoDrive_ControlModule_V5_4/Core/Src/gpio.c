/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, HS_DR_EN_Pin|HS_E_EN_Pin|HS_SC_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_OUT_GPIO_Port, LED_OUT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(EStopLED_CTRL_INT_GPIO_Port, EStopLED_CTRL_INT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BlueButton_Pin HS_E_PG_Pin HS_SC_FT_Pin */
  GPIO_InitStruct.Pin = BlueButton_Pin|HS_E_PG_Pin|HS_SC_FT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : HS_DR_EN_Pin HS_E_EN_Pin HS_SC_EN_Pin */
  GPIO_InitStruct.Pin = HS_DR_EN_Pin|HS_E_EN_Pin|HS_SC_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_OUT_Pin */
  GPIO_InitStruct.Pin = LED_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : EStop_NO_INT_Pin EStop_NC_INT_Pin ENDSTOP_EH_L_NO_INT_Pin ENDSTOP_EH_H_NO_INT_Pin
                           ENDSTOP_EH_H_NC_INT_Pin */
  GPIO_InitStruct.Pin = EStop_NO_INT_Pin|EStop_NC_INT_Pin|ENDSTOP_EH_L_NO_INT_Pin|ENDSTOP_EH_H_NO_INT_Pin
                          |ENDSTOP_EH_H_NC_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : EStopLED_CTRL_INT_Pin */
  GPIO_InitStruct.Pin = EStopLED_CTRL_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(EStopLED_CTRL_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : VBUCK_CTRL_Pin */
  GPIO_InitStruct.Pin = VBUCK_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(VBUCK_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HS_DR_FT_Pin HS_SC_PG_Pin ENDSTOP_SC_H_NO_INT_Pin ENDSTOP_SC_H_NC_INT_Pin
                           ENDSTOP_EP_L_NO_INT_Pin ENDSTOP_EP_L_NC_INT_Pin ENDSTOP_EP_H_NO_INT_Pin ENDSTOP_EP_H_NC_INT_Pin */
  GPIO_InitStruct.Pin = HS_DR_FT_Pin|HS_SC_PG_Pin|ENDSTOP_SC_H_NO_INT_Pin|ENDSTOP_SC_H_NC_INT_Pin
                          |ENDSTOP_EP_L_NO_INT_Pin|ENDSTOP_EP_L_NC_INT_Pin|ENDSTOP_EP_H_NO_INT_Pin|ENDSTOP_EP_H_NC_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : HS_DR_PG_Pin HS_E_FT_Pin */
  GPIO_InitStruct.Pin = HS_DR_PG_Pin|HS_E_FT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
