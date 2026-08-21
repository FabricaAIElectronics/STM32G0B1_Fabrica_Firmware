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
#include "board_pins.h"   /* annotated pin rationale; defines defer to the ones below */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* Peripheral handles shared with the hand-written modules. */
extern FDCAN_HandleTypeDef hfdcan1;
extern I2C_HandleTypeDef   hi2c1;      /* host port (slave at 0x51)      */
extern I2C_HandleTypeDef   hi2c3;      /* AT24C256 EEPROM (master)        */
extern TIM_HandleTypeDef   htim2;      /* encoder                         */
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
#define BLUE_BUTTON_Pin GPIO_PIN_13
#define BLUE_BUTTON_GPIO_Port GPIOC
#define BUFF_SEL_1_Pin GPIO_PIN_0
#define BUFF_SEL_1_GPIO_Port GPIOC
#define BUFF_SEL_2_Pin GPIO_PIN_1
#define BUFF_SEL_2_GPIO_Port GPIOC
#define ENC_A_Pin GPIO_PIN_0
#define ENC_A_GPIO_Port GPIOA
#define ENC_B_Pin GPIO_PIN_1
#define ENC_B_GPIO_Port GPIOA
#define ENC_BUT_Pin GPIO_PIN_2
#define ENC_BUT_GPIO_Port GPIOA
#define LED_OUT_Pin GPIO_PIN_5
#define LED_OUT_GPIO_Port GPIOA
#define EEPROM_SDA_Pin GPIO_PIN_6
#define EEPROM_SDA_GPIO_Port GPIOA
#define EEPROM_SCL_Pin GPIO_PIN_7
#define EEPROM_SCL_GPIO_Port GPIOA
#define ROT_SW_0_Pin GPIO_PIN_5
#define ROT_SW_0_GPIO_Port GPIOC
#define ROT_SW_1_Pin GPIO_PIN_0
#define ROT_SW_1_GPIO_Port GPIOB
#define ROT_SW_2_Pin GPIO_PIN_1
#define ROT_SW_2_GPIO_Port GPIOB
#define ROT_SW_3_Pin GPIO_PIN_2
#define ROT_SW_3_GPIO_Port GPIOB
#define ROT_SW_4_Pin GPIO_PIN_10
#define ROT_SW_4_GPIO_Port GPIOB
#define ROT_SW_5_Pin GPIO_PIN_11
#define ROT_SW_5_GPIO_Port GPIOB
#define ROT_SW_6_Pin GPIO_PIN_12
#define ROT_SW_6_GPIO_Port GPIOB
#define BUTTON_1_Pin GPIO_PIN_13
#define BUTTON_1_GPIO_Port GPIOB
#define BUTTON_2_Pin GPIO_PIN_14
#define BUTTON_2_GPIO_Port GPIOB
#define BUTTON_3_Pin GPIO_PIN_15
#define BUTTON_3_GPIO_Port GPIOB
#define BUTTON_4_Pin GPIO_PIN_8
#define BUTTON_4_GPIO_Port GPIOA
#define BUTTON_5_Pin GPIO_PIN_9
#define BUTTON_5_GPIO_Port GPIOA
#define BUTTON_6_Pin GPIO_PIN_6
#define BUTTON_6_GPIO_Port GPIOC
#define BUTTON_INT_1_Pin GPIO_PIN_7
#define BUTTON_INT_1_GPIO_Port GPIOC
#define BUTTON_INT_2_Pin GPIO_PIN_8
#define BUTTON_INT_2_GPIO_Port GPIOD
#define BUTTON_INT_3_Pin GPIO_PIN_9
#define BUTTON_INT_3_GPIO_Port GPIOD
#define BUTTON_INT_4_Pin GPIO_PIN_10
#define BUTTON_INT_4_GPIO_Port GPIOA
#define TOGGLE_1_Pin GPIO_PIN_11
#define TOGGLE_1_GPIO_Port GPIOA
#define TOGGLE_2_Pin GPIO_PIN_12
#define TOGGLE_2_GPIO_Port GPIOA
#define TOGGLE_3_Pin GPIO_PIN_15
#define TOGGLE_3_GPIO_Port GPIOA
#define TOGGLE_4_Pin GPIO_PIN_8
#define TOGGLE_4_GPIO_Port GPIOC
#define LED_1_Pin GPIO_PIN_9
#define LED_1_GPIO_Port GPIOC
#define LED_2_Pin GPIO_PIN_0
#define LED_2_GPIO_Port GPIOD
#define LED_3_Pin GPIO_PIN_1
#define LED_3_GPIO_Port GPIOD
#define LED_4_Pin GPIO_PIN_2
#define LED_4_GPIO_Port GPIOD
#define LED_5_Pin GPIO_PIN_3
#define LED_5_GPIO_Port GPIOD
#define LED_6_Pin GPIO_PIN_4
#define LED_6_GPIO_Port GPIOD
#define LED_INT_1_Pin GPIO_PIN_5
#define LED_INT_1_GPIO_Port GPIOD
#define LED_INT_2_Pin GPIO_PIN_6
#define LED_INT_2_GPIO_Port GPIOD
#define LED_INT_3_Pin GPIO_PIN_3
#define LED_INT_3_GPIO_Port GPIOB
#define LED_INT_4_Pin GPIO_PIN_4
#define LED_INT_4_GPIO_Port GPIOB
#define HOST_SCL_Pin GPIO_PIN_6
#define HOST_SCL_GPIO_Port GPIOB
#define HOST_SDA_Pin GPIO_PIN_7
#define HOST_SDA_GPIO_Port GPIOB
#define CAN_RX_Pin GPIO_PIN_8
#define CAN_RX_GPIO_Port GPIOB
#define CAN_TX_Pin GPIO_PIN_9
#define CAN_TX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
