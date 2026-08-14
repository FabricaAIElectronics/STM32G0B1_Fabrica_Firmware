/**
  ******************************************************************************
  * @file    stm32g0xx_hal_msp.c
  * @brief   Peripheral-to-board wiring: clocks, pin alternate functions, NVIC.
  ******************************************************************************
  */

#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

/**
  * @brief FDCAN1 on PB8 (RX) / PB9 (TX), AF3.
  *
  * The kernel clock is taken from PCLK1 so the bit timing in MX_FDCAN1_Init
  * is derived from the same 60 MHz the rest of the system runs on. Selecting
  * HSI or PLLQ here instead would silently change the baud rate.
  */
void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (hfdcan->Instance == FDCAN1)
    {
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
        PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PCLK1;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            Error_Handler();
        }

        __HAL_RCC_FDCAN_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* RX carries a pull-up: if the TJA1057 is unpowered or the bus is
         * disconnected, the receiver input would otherwise float and the
         * peripheral would see a stream of phantom dominant bits. */
        GPIO_InitStruct.Pin       = CAN_RX_Pin;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_PULLUP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF3_FDCAN1;
        HAL_GPIO_Init(CAN_RX_GPIO_Port, &GPIO_InitStruct);

        GPIO_InitStruct.Pin       = CAN_TX_Pin;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF3_FDCAN1;
        HAL_GPIO_Init(CAN_TX_GPIO_Port, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(TIM16_FDCAN_IT0_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(TIM16_FDCAN_IT0_IRQn);
    }
}

/** I2C1 on PB6 (SCL) / PB7 (SDA), AF6, open drain. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (hi2c->Instance == I2C1)
    {
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
        PeriphClkInit.I2c1ClockSelection   = RCC_I2C1CLKSOURCE_PCLK1;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            Error_Handler();
        }

        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* R11/R12 give 10 k on-board. The internal pull-ups are enabled as
         * well — roughly 8 k combined — because P1 takes this bus off the
         * board and 10 k alone is weak for any cable length. */
        GPIO_InitStruct.Pin       = (uint16_t)(I2C_SCL_Pin | I2C_SDA_Pin);
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull      = GPIO_PULLUP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF6_I2C1;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        __HAL_RCC_I2C1_CLK_ENABLE();
    }
}

/**
  * @brief TIM2 encoder inputs on PA0 (CH1) / PA1 (CH2), AF2.
  *
  * NOPULL: R32/R33 already pull both channels to VCC through 1 k, and the
  * encoder's common pin is grounded, so the externals define the idle level
  * far more stiffly than an internal pull could.
  */
void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (htim->Instance == TIM2)
    {
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin       = (uint16_t)(ENC_A_Pin | ENC_B_Pin);
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF2_TIM2;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}
