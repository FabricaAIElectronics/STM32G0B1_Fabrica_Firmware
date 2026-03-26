/**
 * @file    Power_Electronic.c
 * @brief   High-side power control, analog sensing, and CAN telemetry packers.
 *
 * @author  jordan
 * @date    2026-03-25
 */

#include "Power_Electronic.h"
#include "main.h"
#include "adc.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  ADC DMA buffer  (written continuously by hardware)
 * ═══════════════════════════════════════════════════════════════════════ */

volatile uint16_t ADC_VAL[ADC_BUF_LEN];

/* ═══════════════════════════════════════════════════════════════════════
 *  ADC conversion constants
 * ═══════════════════════════════════════════════════════════════════════ */

#define VOLTAGE_REF             3.3f
#define ADC_MAX                 4095.0f

/* TPS2493 current sense: gain = 48, Rsense = 3 mOhm → factor = 0.144 */
#define TPS2493_CURRENT_SENSE_FACTOR    0.144f

/* Voltage divider ratio on the current sense output: (R1+R2)/R2 */
#define CURRENT_SENSE_VD_RATIO          (110.0f / 100.0f)

/* Voltage divider ratio for 24V/12V bus measurement: (R1+R2)/R2 */
#define BUS_VOLTAGE_VD_RATIO            (222.0f / 22.0f)

/* Multiplier to convert volts → 0.1V units (one decimal place) */
#define CORRECTION_FACTOR_1DP           10.0f

/* ═══════════════════════════════════════════════════════════════════════
 *  High-side power module enable / disable
 * ═══════════════════════════════════════════════════════════════════════ */

int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{
    (void)timeout_ms;   /* non-blocking: host monitors PG via GPIO broadcast */

    switch (module) {
    case HS_MODULE_DRIVE:
        HAL_GPIO_WritePin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin, GPIO_PIN_SET);
        break;
    case HS_MODULE_EXTRUDER:
        HAL_GPIO_WritePin(HS_E_EN_GPIO_Port, HS_E_EN_Pin, GPIO_PIN_SET);
        break;
    case HS_MODULE_SCRUBBING:
        HAL_GPIO_WritePin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin, GPIO_PIN_SET);
        break;
    default:
        return PE_ERR_INVALID_PARAM;
    }

    return PE_SUCCESS;
}

int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{
    (void)timeout_ms;

    switch (module) {
    case HS_MODULE_DRIVE:
        HAL_GPIO_WritePin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin, GPIO_PIN_RESET);
        break;
    case HS_MODULE_EXTRUDER:
        HAL_GPIO_WritePin(HS_E_EN_GPIO_Port, HS_E_EN_Pin, GPIO_PIN_RESET);
        break;
    case HS_MODULE_SCRUBBING:
        HAL_GPIO_WritePin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin, GPIO_PIN_RESET);
        break;
    default:
        return PE_ERR_INVALID_PARAM;
    }

    return PE_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  ADC calibration and DMA start
 * ═══════════════════════════════════════════════════════════════════════ */

int Calibrate_ADC1(void)
{
    HAL_ADC_Stop(&hadc1);
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
        return PE_ERR_GEN;
    return PE_SUCCESS;
}

int Start_ADC1_DMA(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADC_VAL, ADC_BUF_LEN) != HAL_OK)
        return PE_ERR_GEN;
    return PE_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Current and voltage readings
 * ═══════════════════════════════════════════════════════════════════════ */

int Read_HighSide_Module_Current_mA(HighSide_Module_t module, uint32_t *current_mA)
{
    uint16_t adc_value;

    switch (module) {
    case HS_MODULE_DRIVE:     adc_value = ADC_VAL[CURR_MON_1]; break;
    case HS_MODULE_EXTRUDER:  adc_value = ADC_VAL[CURR_MON_2]; break;
    case HS_MODULE_SCRUBBING: adc_value = ADC_VAL[CURR_MON_3]; break;
    default:
        return PE_ERR_INVALID_PARAM;
    }

    float voltage = (adc_value / ADC_MAX) * VOLTAGE_REF;
    float current = (voltage * CURRENT_SENSE_VD_RATIO) / TPS2493_CURRENT_SENSE_FACTOR;
    *current_mA   = (uint32_t)(current * 1000.0f);

    return PE_SUCCESS;
}

void Read_24V_Bus_Current_mA(uint32_t *current_mA)
{
    uint16_t adc_value = ADC_VAL[CURR_MON_24V];
    float voltage = (adc_value / ADC_MAX) * VOLTAGE_REF;
    float current = (voltage * CURRENT_SENSE_VD_RATIO) / TPS2493_CURRENT_SENSE_FACTOR;
    *current_mA   = (uint32_t)(current * 1000.0f);
}

void Read_24V_Voltage_1DP(uint32_t *voltage_1DP)
{
    uint16_t adc_value = ADC_VAL[VADC_24];
    float voltage = (adc_value / ADC_MAX) * VOLTAGE_REF;
    voltage = voltage * BUS_VOLTAGE_VD_RATIO * CORRECTION_FACTOR_1DP;
    *voltage_1DP = (uint32_t)voltage;
}

void Read_12V_Voltage_1DP(uint32_t *voltage_1DP)
{
    uint16_t adc_value = ADC_VAL[VADC_12];
    float voltage = (adc_value / ADC_MAX) * VOLTAGE_REF;
    voltage = voltage * BUS_VOLTAGE_VD_RATIO * CORRECTION_FACTOR_1DP;
    *voltage_1DP = (uint32_t)voltage;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Thermistor temperature  (Steinhart-Hart B-parameter)
 *
 *  Actual circuit topology:
 *
 *    12V ─── NTC(R_ntc) ─── Node_A (RPTC)
 *                               │── R80 (100 kΩ) ── GND
 *                               └── R79 (180 kΩ) ── Node_B
 *                                                       │── R82 (62 kΩ) ── GND
 *                                                       └── R81 (2.2 kΩ) ── VPTC → ADC
 *                                                       │── D10 (clamp to VCC 3.3V)
 *
 *  Step 1 — ADC → V_VPTC:
 *    V_VPTC = ADC * VREF / 4096
 *
 *  Step 2 — undo the attenuator to recover V_Node_A:
 *    V_Node_A = V_VPTC * (R79 + R82) / R82
 *
 *  Step 3 — recover R_ntc from the voltage divider at Node_A:
 *    V_Node_A = 12V * R_load / (R_ntc + R_load)
 *    where R_load = R80 || (R79 + R82)
 *    → R_ntc = R_load * (12V / V_Node_A − 1)
 *
 *  Step 4 — Beta equation: R_ntc → °C
 *    1/T = 1/T0 + ln(R_ntc/R0) / B
 * ═══════════════════════════════════════════════════════════════════════ */

int Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, int32_t *temperature_C)
{
    if (thermistor < TEMP_PTC_1 || thermistor > TEMP_PTC_6)
        return PE_ERR_INVALID_PARAM;

    uint16_t adc_value = ADC_VAL[thermistor];
    if (adc_value == 0U)
        return PE_ERR_GEN;

    /* ── Circuit constants ─────────────────────────────────────────────── */
    const float V_SUPPLY = 12.0f;
    const float R80      = 100000.0f;
    const float R79      = 180000.0f;
    const float R82      =  62000.0f;
    /* R_load = R80 || (R79 + R82) */
    const float R_chain  = R79 + R82;                             /* 242 kΩ */
    const float R_load   = (R80 * R_chain) / (R80 + R_chain);    /* 70 760 Ω */

    /* ── Step 1: ADC → V_VPTC ─────────────────────────────────────────── */
    float v_vptc = ((float)adc_value / 4095.0f) * VOLTAGE_REF;

    /* Guard: below noise floor → sensor open or disconnected */
    if (v_vptc < 0.010f)
        return PE_ERR_GEN;

    /* ── Step 2: undo attenuator → V_Node_A ──────────────────────────── */
    float v_node_a = v_vptc * R_chain / R82;

    /* Guard: must stay below supply (would indicate ADC saturation) */
    if (v_node_a >= V_SUPPLY)
        return PE_ERR_GEN;

    /* ── Step 3: recover NTC resistance ──────────────────────────────── */
    float resistance = R_load * ((V_SUPPLY / v_node_a) - 1.0f);

    if (resistance <= 0.0f)
        return PE_ERR_GEN;

    /* ── Step 4: Beta equation → °C ──────────────────────────────────── */
    const float R0 = 100000.0f;   /* resistance at 25°C */
    const float T0 = 298.15f;     /* 25°C in Kelvin */
    const float B  = 3950.0f;     /* NTC beta value */

    float inv_T = (1.0f / T0) + (logf(resistance / R0) / B);

    if (inv_T <= 0.0f)
        return PE_ERR_GEN;

    float temp_C = (1.0f / inv_T) - 273.15f;

    if (temp_C < -40.0f || temp_C > 200.0f)
        return PE_ERR_GEN;

    *temperature_C = (int32_t)temp_C;
    return PE_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  12 V buck converter
 * ═══════════════════════════════════════════════════════════════════════ */

int Enable_12V_Buck_Converter(void)
{
    uint32_t bus_24V_1DP = 0;
    Read_24V_Voltage_1DP(&bus_24V_1DP);

    const uint32_t MIN_24V_FOR_12V = 200;   /* 20.0 V minimum */
    if (bus_24V_1DP < MIN_24V_FOR_12V)
        return PE_ERR_GEN;

    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_SET);
    return PE_SUCCESS;
}

void Disable_12V_Buck_Converter(void)
{
    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_RESET);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CAN telemetry data packers
 * ═══════════════════════════════════════════════════════════════════════ */

size_t CAN_Packer_24V_Bus_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t v_1DP = 0;
    Read_24V_Voltage_1DP(&v_1DP);

    uint16_t val = (v_1DP > 65535U) ? 65535U : (uint16_t)v_1DP;
    out[0] = (uint8_t)(val & 0xFF);
    out[1] = (uint8_t)((val >> 8) & 0xFF);
    return 2;
}

size_t CAN_Packer_12V_Bus_1Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint32_t v_1DP = 0;
    Read_12V_Voltage_1DP(&v_1DP);

    out[0] = (v_1DP > 255U) ? 255U : (uint8_t)v_1DP;
    return 1;
}

size_t CAN_Packer_HighSide_Module_Current_1DP_1Byte(HighSide_Module_t module,
                                                     uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint32_t current_mA = 0;
    if (Read_HighSide_Module_Current_mA(module, &current_mA) != PE_SUCCESS)
        return 0;

    uint32_t current_1dp = (current_mA + 50U) / 100U;
    if (current_1dp > 300U) current_1dp = 300U;

    out[0] = (current_1dp > 255U) ? 255U : (uint8_t)current_1dp;
    return 1;
}

size_t CAN_Packer_24V_Bus_Current_1DP_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t current_mA = 0;
    Read_24V_Bus_Current_mA(&current_mA);

    uint16_t current_1dp = (uint16_t)((current_mA + 50U) / 100U);
    out[0] = (uint8_t)(current_1dp & 0xFF);
    out[1] = (uint8_t)((current_1dp >> 8) & 0xFF);
    return 2;
}

size_t CAN_Packer_Thermistor_Temp_1Byte(ADC_Peripheral_t thermistor,
                                         uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    int32_t temperature_C = 0;
    if (Read_Thermistor_Temperature_C(thermistor, &temperature_C) != PE_SUCCESS)
        return 0;

    /* Offset encoding: wire = degC + 40  (so -40 degC → 0x00, 25 degC → 65) */
    int32_t encoded = temperature_C + 40;
    if (encoded < 0)   encoded = 0;
    if (encoded > 255)  encoded = 255;

    out[0] = (uint8_t)encoded;
    return 1;
}
