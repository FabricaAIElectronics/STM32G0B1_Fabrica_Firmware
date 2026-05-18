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
void MX_FDCAN1_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ENDSTOP_EH_LNC_INT1_Pin GPIO_PIN_11
#define ENDSTOP_EH_LNC_INT1_GPIO_Port GPIOC
#define FAN_PWM_5_Pin GPIO_PIN_12
#define FAN_PWM_5_GPIO_Port GPIOC
#define BlueButton_Pin GPIO_PIN_13
#define BlueButton_GPIO_Port GPIOC
#define Toggle_PosDetect_Pin GPIO_PIN_0
#define Toggle_PosDetect_GPIO_Port GPIOC
#define HS_DR_EN_Pin GPIO_PIN_1
#define HS_DR_EN_GPIO_Port GPIOC
#define HS_E_EN_Pin GPIO_PIN_2
#define HS_E_EN_GPIO_Port GPIOC
#define HS_SC_EN_Pin GPIO_PIN_3
#define HS_SC_EN_GPIO_Port GPIOC
#define PTC_1_Pin GPIO_PIN_0
#define PTC_1_GPIO_Port GPIOA
#define PTC_2_Pin GPIO_PIN_1
#define PTC_2_GPIO_Port GPIOA
#define PTC_3_Pin GPIO_PIN_2
#define PTC_3_GPIO_Port GPIOA
#define PTC_4_Pin GPIO_PIN_3
#define PTC_4_GPIO_Port GPIOA
#define PTC_5_Pin GPIO_PIN_4
#define PTC_5_GPIO_Port GPIOA
#define LED_OUT_Pin GPIO_PIN_5
#define LED_OUT_GPIO_Port GPIOA
#define PTC_6_Pin GPIO_PIN_6
#define PTC_6_GPIO_Port GPIOA
#define CURR_MON_1_Pin GPIO_PIN_7
#define CURR_MON_1_GPIO_Port GPIOA
#define TACHO_1_Pin GPIO_PIN_4
#define TACHO_1_GPIO_Port GPIOC
#define TACHO_2_Pin GPIO_PIN_5
#define TACHO_2_GPIO_Port GPIOC
#define CURR_MON_2_Pin GPIO_PIN_0
#define CURR_MON_2_GPIO_Port GPIOB
#define CURR_MON_3_Pin GPIO_PIN_1
#define CURR_MON_3_GPIO_Port GPIOB
#define VADC_24_Pin GPIO_PIN_2
#define VADC_24_GPIO_Port GPIOB
#define VADC_12_Pin GPIO_PIN_10
#define VADC_12_GPIO_Port GPIOB
#define TACHO_4_Pin GPIO_PIN_11
#define TACHO_4_GPIO_Port GPIOB
#define CURR_MON_IN_Pin GPIO_PIN_12
#define CURR_MON_IN_GPIO_Port GPIOB
#define EStop_NO_INT_Pin GPIO_PIN_13
#define EStop_NO_INT_GPIO_Port GPIOB
#define EStop_NC_INT_Pin GPIO_PIN_14
#define EStop_NC_INT_GPIO_Port GPIOB
#define EStopLED_CTRL_INT_Pin GPIO_PIN_15
#define EStopLED_CTRL_INT_GPIO_Port GPIOB
#define FAN_PWM_1_Pin GPIO_PIN_8
#define FAN_PWM_1_GPIO_Port GPIOA
#define FAN_PWM_2_Pin GPIO_PIN_9
#define FAN_PWM_2_GPIO_Port GPIOA
#define TACHO_3_Pin GPIO_PIN_6
#define TACHO_3_GPIO_Port GPIOC
#define TACHO_5_Pin GPIO_PIN_7
#define TACHO_5_GPIO_Port GPIOC
#define VBUCK_CTRL_Pin GPIO_PIN_8
#define VBUCK_CTRL_GPIO_Port GPIOD
#define HS_DR_FT_Pin GPIO_PIN_9
#define HS_DR_FT_GPIO_Port GPIOD
#define FAN_PWM_3_Pin GPIO_PIN_10
#define FAN_PWM_3_GPIO_Port GPIOA
#define FAN_PWM_4_Pin GPIO_PIN_11
#define FAN_PWM_4_GPIO_Port GPIOA
#define HS_DR_PG_Pin GPIO_PIN_12
#define HS_DR_PG_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define HS_E_FT_Pin GPIO_PIN_15
#define HS_E_FT_GPIO_Port GPIOA
#define HS_E_PG_Pin GPIO_PIN_8
#define HS_E_PG_GPIO_Port GPIOC
#define HS_SC_FT_Pin GPIO_PIN_9
#define HS_SC_FT_GPIO_Port GPIOC
#define HS_SC_PG_Pin GPIO_PIN_0
#define HS_SC_PG_GPIO_Port GPIOD
#define ENDSTOP_SC_H_NO_INT_Pin GPIO_PIN_1
#define ENDSTOP_SC_H_NO_INT_GPIO_Port GPIOD
#define ENDSTOP_SC_H_NC_INT_Pin GPIO_PIN_2
#define ENDSTOP_SC_H_NC_INT_GPIO_Port GPIOD
#define ENDSTOP_EP_L_NO_INT_Pin GPIO_PIN_3
#define ENDSTOP_EP_L_NO_INT_GPIO_Port GPIOD
#define ENDSTOP_EP_L_NC_INT_Pin GPIO_PIN_4
#define ENDSTOP_EP_L_NC_INT_GPIO_Port GPIOD
#define ENDSTOP_EP_H_NO_INT_Pin GPIO_PIN_5
#define ENDSTOP_EP_H_NO_INT_GPIO_Port GPIOD
#define ENDSTOP_EP_H_NC_INT_Pin GPIO_PIN_6
#define ENDSTOP_EP_H_NC_INT_GPIO_Port GPIOD
#define ENDSTOP_EH_L_NO_INT_Pin GPIO_PIN_3
#define ENDSTOP_EH_L_NO_INT_GPIO_Port GPIOB
#define ENDSTOP_EH_H_NO_INT_Pin GPIO_PIN_4
#define ENDSTOP_EH_H_NO_INT_GPIO_Port GPIOB
#define ENDSTOP_EH_H_NC_INT_Pin GPIO_PIN_5
#define ENDSTOP_EH_H_NC_INT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
