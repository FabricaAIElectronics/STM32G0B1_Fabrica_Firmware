/**
  ******************************************************************************
  * @file    main.c
  * @brief   Application entry point — fabricaAI calibration knob / button
  *          board V5.5, STM32G0B1RET6.
  *
  * This image is the OpenBLT *user program*. It is linked to start at
  * 0x08003000, 12 KB above the bottom of flash, because the bootloader owns
  * sectors 0-5. Nothing here may be flashed to 0x08000000.
  ******************************************************************************
  */

#include "main.h"
#include "applogic.h"
#include "can_operation.h"
#include "inputs.h"
#include "leds.h"
#include "i2c_host.h"
#include "i2c_host_proto.h"

FDCAN_HandleTypeDef hfdcan1;
I2C_HandleTypeDef   hi2c1;
I2C_HandleTypeDef   hi2c3;      /* EEPROM bus, PA6/PA7, 100 kHz */
TIM_HandleTypeDef   htim2;

static AppStateMachine sm;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_TIM2_Init(void);

/**
  * @brief Point the vector table at this image.
  *
  * The bootloader has already run and its own vector table sits at
  * 0x08000000. Every interrupt taken after this line — SysTick, FDCAN —
  * must dispatch through the application's table at 0x08003000 instead, so
  * this has to happen before HAL_Init() enables the tick.
  */
static void VectorBase_Config(void)
{
    extern const unsigned long g_pfnVectors[];
    SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}

int main(void)
{
    VectorBase_Config();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();

    /* Take the LED nets to a defined state as early as possible. Until this
     * runs, BUFF_SEL_1 has no hardware default (R51 is DNP) and both buffer
     * banks may be enabled onto the same nets. */
    Leds_Init();

    MX_FDCAN1_Init();
    MX_I2C1_Init();
    I2CHost_Init();      /* slave listen for the V5.2-compatible host port */
    MX_I2C3_Init();
    MX_TIM2_Init();

    CAN_Init();          /* filter + RX FIFO0 interrupt + HAL_FDCAN_Start */
    AppLogic_Init(&sm);

    while (1)
    {
        /* Green heartbeat on PA5 — the fastest way to tell "the application
         * is running" from "the bootloader is waiting" at a glance. */
        {
            static uint32_t led_last_tick = 0U;
            const uint32_t now = HAL_GetTick();
            if ((now - led_last_tick) >= 500U) {
                led_last_tick = now;
                HAL_GPIO_TogglePin(LED_OUT_GPIO_Port, LED_OUT_Pin);
            }
        }

        AppLogic_Run(&sm);
    }
}

/**
  * @brief System clock: HSE 8 MHz crystal (Y2, ABM3B-8.000MHZ) -> PLL -> 60 MHz.
  *
  * PLL: /1 x15 /2 => VCO 120 MHz, SYSCLK 60 MHz. Identical to the LEDDriver
  * and PowerStage builds, which matters because the FDCAN bit timing below is
  * derived from PCLK1 and every Fabrica board has to land on exactly 500
  * kbit/s to share the bus.
  *
  * CSS is enabled: if the crystal stops, the system falls back to HSI rather
  * than halting. CAN timing then drifts by the HSI tolerance and the node
  * will drop off the bus — but the board stays alive and its heartbeat LED
  * keeps blinking, which is a far easier fault to diagnose than a dead board.
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType   = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState         = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState     = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource    = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM         = RCC_PLLM_DIV1;
    RCC_OscInitStruct.PLL.PLLN         = 15;
    RCC_OscInitStruct.PLL.PLLP         = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ         = RCC_PLLQ_DIV8;
    RCC_OscInitStruct.PLL.PLLR         = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }

    HAL_RCC_EnableCSS();
}

/**
  * @brief FDCAN1: classic CAN, 500 kbit/s nominal.
  *
  * Kernel clock = PCLK1 = 60 MHz. Prescaler 8 gives a 7.5 MHz time-quantum
  * clock; 1 + 10 + 4 = 15 tq per bit gives 500 kbit/s with the sample point
  * at 11/15 = 73.3 %, comfortably inside the 75 % target in Docs/CAN_Bus.md.
  *
  * AutoRetransmission is DISABLE, matching the other Fabrica boards: this is
  * periodic telemetry, and a frame that lost arbitration is better replaced
  * by the next 100 ms sample than retried with stale contents.
  */
static void MX_FDCAN1_Init(void)
{
    hfdcan1.Instance                  = FDCAN1;
    hfdcan1.Init.ClockDivider         = FDCAN_CLOCK_DIV1;
    hfdcan1.Init.FrameFormat          = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode                 = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission   = DISABLE;
    hfdcan1.Init.TransmitPause        = DISABLE;
    hfdcan1.Init.ProtocolException    = DISABLE;
    hfdcan1.Init.NominalPrescaler     = 8;
    hfdcan1.Init.NominalSyncJumpWidth = 1;
    hfdcan1.Init.NominalTimeSeg1      = 10;
    hfdcan1.Init.NominalTimeSeg2      = 4;
    hfdcan1.Init.DataPrescaler        = 1;
    hfdcan1.Init.DataSyncJumpWidth    = 1;
    hfdcan1.Init.DataTimeSeg1         = 1;
    hfdcan1.Init.DataTimeSeg2         = 1;
    /* One range filter is installed by CAN_Init(); declaring the slot here is
     * what makes the message-RAM allocation include it. */
    hfdcan1.Init.StdFiltersNbr        = 1;
    hfdcan1.Init.ExtFiltersNbr        = 0;
    hfdcan1.Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }
}

/* AT24C256 EEPROM (IC1) and the external header P1. 0x00B01E60 is the
 * CubeMX-computed 100 kHz timing for a 60 MHz kernel clock — the same value
 * the LEDDriver board runs. P1 leaves the box, so 100 kHz rather than 400 kHz
 * also buys margin on cable capacitance. */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x00B01E60;
    hi2c1.Init.OwnAddress1      = (uint16_t)(I2CHOST_ADDR_7BIT << 1);
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        Error_Handler();
    }
}

/** I2C3: AT24C256 EEPROM, standard-mode 100 kHz from the 60 MHz kernel clock. */
static void MX_I2C3_Init(void)
{
    hi2c3.Instance              = I2C3;
    hi2c3.Init.Timing           = 0x10A077A8;
    hi2c3.Init.OwnAddress1      = 0;
    hi2c3.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2      = 0;
    hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c3.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c3) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief TIM2 in quadrature encoder mode on PA0/PA1.
  *
  * TI12 counts on both edges of both channels — x4 decoding, so a PEC11L
  * gives 4 counts per detent. inputs.c divides that back down.
  *
  * The input filter is at its maximum (15 = 8 samples at f_DTS/32). A panel
  * encoder on a hand-turned knob produces nothing near the frequencies that
  * filter rejects, so there is no reason to run it any lower, and contact
  * bounce on a PEC11L is exactly what it is there to swallow.
  *
  * TIM2 is 32-bit on this part but Period is deliberately 0xFFFF: inputs.c
  * accumulates deltas in software, and a 16-bit counter makes the wrap
  * arithmetic a plain int16 subtraction.
  */
static void MX_TIM2_Init(void)
{
    TIM_Encoder_InitTypeDef sConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 0xFFFF;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    sConfig.EncoderMode  = TIM_ENCODERMODE_TI12;
    sConfig.IC1Polarity  = TIM_ICPOLARITY_RISING;
    sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
    sConfig.IC1Filter    = 15;
    sConfig.IC2Polarity  = TIM_ICPOLARITY_RISING;
    sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
    sConfig.IC2Filter    = 15;
    if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK) {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief GPIO configuration.
  *
  * Output levels are written to ODR BEFORE the pins are switched to output
  * mode. A pin still in its reset (analog/input) state ignores the write but
  * latches it, so the level is already correct on the very first clock edge
  * of output mode — no glitch, and no moment where ten LED lines drive an
  * arbitrary pattern into the buffers.
  *
  * Every input is NOPULL. Each one already has an external pull that defines
  * its idle level (10 k pull-downs on the rotary, 10 k to +5 V through the
  * translators on the buttons, 100 R either way on the toggles, 10 k on
  * ENC_BUT), and an internal pull would only fight it or shift a divider.
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* ---- Pre-set output levels: LEDs dark (high = dark, inverting buffer),
     *      direct path enabled, STM32 path disabled. ---- */
    HAL_GPIO_WritePin(GPIOC, LED_1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD, (uint16_t)(LED_2_Pin | LED_3_Pin | LED_4_Pin |
                                        LED_5_Pin | LED_6_Pin |
                                        LED_INT_1_Pin | LED_INT_2_Pin),
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, (uint16_t)(LED_INT_3_Pin | LED_INT_4_Pin),
                      GPIO_PIN_SET);

    HAL_GPIO_WritePin(BUFF_SEL_1_GPIO_Port, BUFF_SEL_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUFF_SEL_2_GPIO_Port, BUFF_SEL_2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_OUT_GPIO_Port, LED_OUT_Pin, GPIO_PIN_RESET);

    /* ---- Outputs ---- */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = (uint16_t)(LED_1_Pin | BUFF_SEL_1_Pin | BUFF_SEL_2_Pin);
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = (uint16_t)(LED_2_Pin | LED_3_Pin | LED_4_Pin |
                                     LED_5_Pin | LED_6_Pin |
                                     LED_INT_1_Pin | LED_INT_2_Pin);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = (uint16_t)(LED_INT_3_Pin | LED_INT_4_Pin);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LED_OUT_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* ---- Inputs ---- */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    /* Buttons 4, 5, int_4; toggles 1, 2, 3; encoder push-button; all on A. */
    GPIO_InitStruct.Pin = (uint16_t)(BUTTON_4_Pin | BUTTON_5_Pin |
                                     BUTTON_INT_4_Pin |
                                     TOGGLE_1_Pin | TOGGLE_2_Pin | TOGGLE_3_Pin |
                                     ENC_BUT_Pin);
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Buttons 1-3 and rotary lines 1-6, all on B. */
    GPIO_InitStruct.Pin = (uint16_t)(BUTTON_1_Pin | BUTTON_2_Pin | BUTTON_3_Pin |
                                     ROT_SW_1_Pin | ROT_SW_2_Pin | ROT_SW_3_Pin |
                                     ROT_SW_4_Pin | ROT_SW_5_Pin | ROT_SW_6_Pin);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Button 6, int_1, toggle 4, rotary line 0, on-board blue button. */
    GPIO_InitStruct.Pin = (uint16_t)(BUTTON_6_Pin | BUTTON_INT_1_Pin |
                                     TOGGLE_4_Pin | ROT_SW_0_Pin |
                                     BLUE_BUTTON_Pin);
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Buttons int_2 and int_3. */
    GPIO_InitStruct.Pin = (uint16_t)(BUTTON_INT_2_Pin | BUTTON_INT_3_Pin);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

/**
  * @brief Unrecoverable init failure.
  *
  * Deliberately does NOT reset. If the clock tree or a peripheral would not
  * come up, resetting produces a board that reboots forever and is very hard
  * to attach a debugger to. Sitting in a tight loop with interrupts off keeps
  * the part attachable over SWD, and the dark heartbeat LED says clearly that
  * the application never reached its main loop.
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
