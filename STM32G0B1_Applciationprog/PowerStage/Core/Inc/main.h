/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define V_LED_PWR_Pin GPIO_PIN_0
#define V_LED_PWR_GPIO_Port GPIOC
#define SW_Pin GPIO_PIN_1
#define SW_GPIO_Port GPIOC
#define CURR_BAT_Pin GPIO_PIN_0
#define CURR_BAT_GPIO_Port GPIOA
#define CURR_CAP_Pin GPIO_PIN_1
#define CURR_CAP_GPIO_Port GPIOA
#define CURR_SBC_Pin GPIO_PIN_2
#define CURR_SBC_GPIO_Port GPIOA
#define CURR_DRIVE_Pin GPIO_PIN_3
#define CURR_DRIVE_GPIO_Port GPIOA
#define CURR_AUX_Pin GPIO_PIN_4
#define CURR_AUX_GPIO_Port GPIOA
#define LED_GREEN_Pin GPIO_PIN_5
#define LED_GREEN_GPIO_Port GPIOA
#define CURR_LED_Pin GPIO_PIN_6
#define CURR_LED_GPIO_Port GPIOA
#define V_24_Pin GPIO_PIN_7
#define V_24_GPIO_Port GPIOA
#define V_CAP_Pin GPIO_PIN_0
#define V_CAP_GPIO_Port GPIOB
#define V_12_Pin GPIO_PIN_1
#define V_12_GPIO_Port GPIOB
#define V_NTC_Pin GPIO_PIN_2
#define V_NTC_GPIO_Port GPIOB
#define FAN_CTRL_PWM_Pin GPIO_PIN_8
#define FAN_CTRL_PWM_GPIO_Port GPIOA
#define FAN_CTRL_Pin GPIO_PIN_9
#define FAN_CTRL_GPIO_Port GPIOA
#define FLT_AUX_Pin GPIO_PIN_8
#define FLT_AUX_GPIO_Port GPIOD
#define PGOOD_AUX_Pin GPIO_PIN_9
#define PGOOD_AUX_GPIO_Port GPIOD
#define EN_AUX_Pin GPIO_PIN_10
#define EN_AUX_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define FLT_LED_Pin GPIO_PIN_9
#define FLT_LED_GPIO_Port GPIOC
#define PGOOD_LED_Pin GPIO_PIN_0
#define PGOOD_LED_GPIO_Port GPIOD
#define EN_LED_Pin GPIO_PIN_1
#define EN_LED_GPIO_Port GPIOD
#define FLT_DRIVE_Pin GPIO_PIN_2
#define FLT_DRIVE_GPIO_Port GPIOD
#define PGOOD_DRIVE_Pin GPIO_PIN_3
#define PGOOD_DRIVE_GPIO_Port GPIOD
#define EN_DRIVE_Pin GPIO_PIN_4
#define EN_DRIVE_GPIO_Port GPIOD
#define FLT_CAP_Pin GPIO_PIN_5
#define FLT_CAP_GPIO_Port GPIOD
#define PGOOD_CAP_Pin GPIO_PIN_6
#define PGOOD_CAP_GPIO_Port GPIOD
#define EN_CAP_Pin GPIO_PIN_3
#define EN_CAP_GPIO_Port GPIOB
#define FLT_SBC_Pin GPIO_PIN_4
#define FLT_SBC_GPIO_Port GPIOB
#define PGOOD_SBC_Pin GPIO_PIN_5
#define PGOOD_SBC_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
/* Alias: bootloader (OpenBLT) uses 'canHandle' for FDCAN1.
 * CubeMX generates 'hfdcan1'.  This macro keeps both in sync so
 * CubeMX can regenerate main.c without breaking app / bootloader code. */
#define canHandle hfdcan1
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
