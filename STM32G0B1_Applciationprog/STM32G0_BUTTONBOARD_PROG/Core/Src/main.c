/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Application entry point - fabricaAI calibration knob / button
  *          board V5.5, STM32G0B1RET6.
  *
  * This image is the OpenBLT *user program*. It is linked to start at
  * 0x08003000, 12 KB above the bottom of flash, because the bootloader owns
  * sectors 0-5. Nothing here may be flashed to 0x08000000.
  *
  * CubeMX-managed file: everything outside the USER CODE fences is generated
  * from STM32G0_BUTTONBOARD_PROG.ioc and will be rewritten by Generate Code.
  * Everything hand-written lives inside a fence. See the project CLAUDE.md.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "applogic.h"
#include "can_operation.h"
#include "inputs.h"
#include "leds.h"
#include "i2c_host.h"
#include "i2c_host_proto.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
FDCAN_HandleTypeDef hfdcan1;

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;

TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
static AppStateMachine sm;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C3_Init(void);
/* USER CODE BEGIN PFP */
static void VectorBase_Config(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
  * @brief Point the vector table at this image.
  *
  * The bootloader has already run and its own vector table sits at
  * 0x08000000. Every interrupt taken after this line - SysTick, FDCAN, I2C1 -
  * must dispatch through the application's table at 0x08003000 instead, so
  * this has to happen before HAL_Init() enables the tick. Hence it lives in
  * USER CODE 1, the fence CubeMX places ahead of HAL_Init().
  */
static void VectorBase_Config(void)
{
    extern const unsigned long g_pfnVectors[];
    SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  VectorBase_Config();
/* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_I2C3_Init();
  /* USER CODE BEGIN 2 */
  /* Slave listen for the V5.2-compatible host port on I2C1. Needs only
   * hi2c1 initialised (MX_I2C1_Init above); ordering after the other
   * MX_*_Init calls is deliberate - nothing here depends on it. */
  I2CHost_Init();

  /* CAN is the one peripheral whose failure is fatal: with it dead the board
   * cannot report anything, so the state machine goes straight to STATE_ERROR
   * (fast heartbeat + reset-retry) instead of running silent. */
  const bool can_ok = CAN_Init();
  AppLogic_Init(&sm, can_ok);
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Green heartbeat on PA5 - the fastest way to tell "the application is
     * running" from "the bootloader is waiting" at a glance. 500 ms when
     * healthy; 100 ms in STATE_ERROR (CAN failed to start), because a board
     * that cannot talk on the bus has no other way to say so. */
    {
      static uint32_t led_last_tick = 0U;
      const uint32_t now = HAL_GetTick();
      const uint32_t period = (sm.state == STATE_ERROR) ? 100U : 500U;
      if ((now - led_last_tick) >= period) {
        led_last_tick = now;
        HAL_GPIO_TogglePin(LED_OUT_GPIO_Port, LED_OUT_Pin);
      }
    }

    AppLogic_Run(&sm);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 15;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */
  /* Classic CAN, 500 kbit/s nominal. Kernel clock = PCLK1 = 60 MHz.
   * Prescaler 8 -> 7.5 MHz tq; 1 + 10 + 4 = 15 tq/bit -> 500 kbit/s, sample
   * point 11/15 = 73.3 %, inside the 75 % target in Docs/CAN_Bus.md.
   * AutoRetransmission DISABLE matches the other Fabrica boards: periodic
   * telemetry is better replaced by the next sample than retried stale.
   * StdFiltersNbr = 1: CAN_Init() installs one range filter; declaring the
   * slot here is what makes the message-RAM allocation include it. All of
   * that is driven from the .ioc - this comment records WHY. */
/* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 8;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 10;
  hfdcan1.Init.NominalTimeSeg2 = 4;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 1;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */
  /* I2C1 = the HOST PORT (connector P1): the STM32 is a pure SLAVE at 7-bit
   * 0x51 speaking the V5.2 ATtiny protocol (i2c_host.c). OwnAddress1 = 0xA2
   * (= 0x51 << 1) is generated from I2C1.OwnAddress in the .ioc - keep it
   * there, not here. 0x00B01E60 is fast-mode-class timing; in pure-slave mode
   * only SDADEL/SCLDEL matter, the master drives SCL. */
/* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00B01E60;
  hi2c1.Init.OwnAddress1 = 162;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */
  /* I2C3 = the AT24C256 config EEPROM (PA6/PA7, AF9 - see MSP), STM32 is
   * the only master, standard-mode 100 kHz from the 60 MHz PCLK1. */
/* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x10A077A8;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xFFFF;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 15;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 15;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, BUFF_SEL_1_Pin|BUFF_SEL_2_Pin|LED_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_OUT_GPIO_Port, LED_OUT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LED_2_Pin|LED_3_Pin|LED_4_Pin|LED_5_Pin
                          |LED_6_Pin|LED_INT_1_Pin|LED_INT_2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_INT_3_Pin|LED_INT_4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BLUE_BUTTON_Pin ROT_SW_0_Pin BUTTON_6_Pin BUTTON_INT_1_Pin
                           TOGGLE_4_Pin */
  GPIO_InitStruct.Pin = BLUE_BUTTON_Pin|ROT_SW_0_Pin|BUTTON_6_Pin|BUTTON_INT_1_Pin
                          |TOGGLE_4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : BUFF_SEL_1_Pin BUFF_SEL_2_Pin LED_1_Pin */
  GPIO_InitStruct.Pin = BUFF_SEL_1_Pin|BUFF_SEL_2_Pin|LED_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : ENC_BUT_Pin BUTTON_4_Pin BUTTON_5_Pin BUTTON_INT_4_Pin
                           TOGGLE_1_Pin TOGGLE_2_Pin TOGGLE_3_Pin */
  GPIO_InitStruct.Pin = ENC_BUT_Pin|BUTTON_4_Pin|BUTTON_5_Pin|BUTTON_INT_4_Pin
                          |TOGGLE_1_Pin|TOGGLE_2_Pin|TOGGLE_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_OUT_Pin */
  GPIO_InitStruct.Pin = LED_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ROT_SW_1_Pin ROT_SW_2_Pin ROT_SW_3_Pin ROT_SW_4_Pin
                           ROT_SW_5_Pin ROT_SW_6_Pin BUTTON_1_Pin BUTTON_2_Pin
                           BUTTON_3_Pin */
  GPIO_InitStruct.Pin = ROT_SW_1_Pin|ROT_SW_2_Pin|ROT_SW_3_Pin|ROT_SW_4_Pin
                          |ROT_SW_5_Pin|ROT_SW_6_Pin|BUTTON_1_Pin|BUTTON_2_Pin
                          |BUTTON_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BUTTON_INT_2_Pin BUTTON_INT_3_Pin */
  GPIO_InitStruct.Pin = BUTTON_INT_2_Pin|BUTTON_INT_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_2_Pin LED_3_Pin LED_4_Pin LED_5_Pin
                           LED_6_Pin LED_INT_1_Pin LED_INT_2_Pin */
  GPIO_InitStruct.Pin = LED_2_Pin|LED_3_Pin|LED_4_Pin|LED_5_Pin
                          |LED_6_Pin|LED_INT_1_Pin|LED_INT_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_INT_3_Pin LED_INT_4_Pin */
  GPIO_InitStruct.Pin = LED_INT_3_Pin|LED_INT_4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* Take the LED nets to a defined state as early as possible - before any
   * other peripheral init runs. Until this, BUFF_SEL_1 has no hardware
   * default (R51 is DNP) and both ACT240 banks may be enabled onto the same
   * nets. This fence runs after the generated pin config above and before
   * MX_FDCAN1_Init/MX_I2C*_Init in main(), which is earlier than the old
   * hand-written order - strictly safer. */
  Leds_Init();
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
