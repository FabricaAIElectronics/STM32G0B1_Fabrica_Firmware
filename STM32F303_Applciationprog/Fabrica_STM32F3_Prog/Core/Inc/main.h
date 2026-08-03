/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "stm32f3xx_hal.h"

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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define Bootloader_ctrl_Pin GPIO_PIN_0
#define Bootloader_ctrl_GPIO_Port GPIOA
#define LD2_Pin GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define ENC_COM_3_Pin GPIO_PIN_6
#define ENC_COM_3_GPIO_Port GPIOA
#define ENC_8_3_Pin GPIO_PIN_7
#define ENC_8_3_GPIO_Port GPIOA
#define ENC_1_1_Pin GPIO_PIN_4
#define ENC_1_1_GPIO_Port GPIOC
#define ENC_2_2_Pin GPIO_PIN_1
#define ENC_2_2_GPIO_Port GPIOB
#define ENC_4_2_Pin GPIO_PIN_2
#define ENC_4_2_GPIO_Port GPIOB
#define ENC_BUT_3_Pin GPIO_PIN_10
#define ENC_BUT_3_GPIO_Port GPIOB
#define ENC_8_2_Pin GPIO_PIN_11
#define ENC_8_2_GPIO_Port GPIOB
#define ENC_COM_2_Pin GPIO_PIN_12
#define ENC_COM_2_GPIO_Port GPIOB
#define ENC_BUT_1_Pin GPIO_PIN_13
#define ENC_BUT_1_GPIO_Port GPIOB
#define ENC_BUT_2_Pin GPIO_PIN_14
#define ENC_BUT_2_GPIO_Port GPIOB
#define ENC_1_2_Pin GPIO_PIN_15
#define ENC_1_2_GPIO_Port GPIOB
#define ENC_4_3_Pin GPIO_PIN_7
#define ENC_4_3_GPIO_Port GPIOC
#define ENC_1_3_Pin GPIO_PIN_8
#define ENC_1_3_GPIO_Port GPIOA
#define ENC_2_3_Pin GPIO_PIN_9
#define ENC_2_3_GPIO_Port GPIOA
#define ENC_2_1_Pin GPIO_PIN_10
#define ENC_2_1_GPIO_Port GPIOA
#define ENC_4_1_Pin GPIO_PIN_3
#define ENC_4_1_GPIO_Port GPIOB
#define ENC_COM_1_Pin GPIO_PIN_4
#define ENC_COM_1_GPIO_Port GPIOB
#define ENC_8_1_Pin GPIO_PIN_5
#define ENC_8_1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
