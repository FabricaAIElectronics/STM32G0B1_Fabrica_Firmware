/**
  ******************************************************************************
  * @file    main.h
  * @brief   Header for main.c — fabricaAI calibration knob / button board V5.5
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"
#include "board_pins.h"

void Error_Handler(void);

extern FDCAN_HandleTypeDef hfdcan1;
extern I2C_HandleTypeDef   hi2c1;
extern TIM_HandleTypeDef   htim2;

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
