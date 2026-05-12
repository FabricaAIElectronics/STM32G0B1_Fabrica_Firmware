/**
 * @file    adc_driver.c
 * @brief   ADC1 calibration and DMA management.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "adc_driver.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

/* DMA destination buffer. Updated continuously by hardware. */
volatile uint16_t ADC_VAL[ADC_BUF_LEN];

int Calibrate_ADC1(void)
{
    HAL_ADC_Stop(&hadc1);
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
        return ADC_ERR_GEN;
    return ADC_SUCCESS;
}

int Start_ADC1_DMA(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADC_VAL, ADC_BUF_LEN) != HAL_OK)
        return ADC_ERR_GEN;
    return ADC_SUCCESS;
}

uint32_t adc_to_mV(uint16_t adc)
{
    return ((uint32_t)adc * 3300U + 2048U) / 4095U;
}
