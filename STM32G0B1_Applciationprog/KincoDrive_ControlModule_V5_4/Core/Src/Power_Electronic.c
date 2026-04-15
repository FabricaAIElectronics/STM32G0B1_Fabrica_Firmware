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

/* Current and voltage conversions are integer-only (no FPU).
 * See adc_to_mV() and mV_to_current_mA() helpers below.
 *
 * Key hardware constants:
 *   VREF              = 3300 mV
 *   ADC full-scale    = 4095 counts
 *   TPS2493 factor    = 144 mV/A  (gain 48 × Rsense 3 mΩ)
 *   IMON divider      = 10 kΩ / 100 kΩ → ×110/100 compensation
 *   Bus voltage div   = (200 kΩ + 22 kΩ) / 22 kΩ = 222/22
 *
 * NOTE: VOLTAGE_REF (float) kept for Steinhart-Hart thermistor math only.
 */
#define VOLTAGE_REF  3.3f   /* used only by thermistor Steinhart-Hart calc */

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
 *  Current and voltage readings  (integer-only, no FPU)
 *
 *  Current:  IMON → 10 kΩ → ADC_pin → 100 kΩ → GND
 *            V_adc = V_imon × 100/110,  so V_imon = V_adc × 110/100
 *            I_mA  = V_imon_mV × 1000 / 144
 *                  = V_adc_mV × 1100 / 144   (110/100 × 1000/144 combined)
 *            Split to avoid uint32 overflow:
 *              v_mV  = (adc × 3300 + 2048) / 4095
 *              I_mA  = (v_mV × 1100 + 72) / 144   (max 3300×1100 = 3.63 M)
 *
 *  Voltage:  V_bus_mV = V_adc_mV × (R1+R2)/R2 = V_adc_mV × 222/22
 *              v_mV  = (adc × 3300 + 2048) / 4095
 *              bus_mV = v_mV × 222 / 22
 * ═══════════════════════════════════════════════════════════════════════ */

/** Convert raw ADC count → millivolts at the ADC pin. */
static inline uint32_t adc_to_mV(uint16_t adc)
{
    return ((uint32_t)adc * 3300U + 2048U) / 4095U;
}

/** Convert ADC-pin millivolts → current in mA (TPS2493 sense path).
 *  Includes 10k/100k divider compensation: V_imon = V_adc × 110/100. */
static inline uint32_t mV_to_current_mA(uint32_t v_adc_mV)
{
    /* 1100 = 110/100 × 1000 (divider comp × mA conversion combined) */
    return (v_adc_mV * 1100U + 72U) / 144U;
}

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

    *current_mA = mV_to_current_mA(adc_to_mV(adc_value));
    return PE_SUCCESS;
}

void Read_24V_Bus_Current_mA(uint32_t *current_mA)
{
    *current_mA = mV_to_current_mA(adc_to_mV(ADC_VAL[CURR_MON_24V]));
}

void Read_24V_Voltage_mV(uint32_t *voltage_mV)
{
    uint32_t v_mV = adc_to_mV(ADC_VAL[VADC_24]);
    *voltage_mV = v_mV * 222U / 22U;
}

void Read_12V_Voltage_mV(uint32_t *voltage_mV)
{
    uint32_t v_mV = adc_to_mV(ADC_VAL[VADC_12]);
    *voltage_mV = v_mV * 222U / 22U;
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
    uint32_t bus_mV = 0;
    Read_24V_Voltage_mV(&bus_mV);

    const uint32_t MIN_24V_FOR_12V_MV = 20000U;   /* 20.0 V minimum */
    if (bus_mV < MIN_24V_FOR_12V_MV)
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

/** Pack uint16 little-endian into 2 bytes. */
static inline void pack_u16_le(uint8_t *out, uint16_t val)
{
    out[0] = (uint8_t)(val & 0xFFU);
    out[1] = (uint8_t)(val >> 8);
}

size_t CAN_Packer_24V_Bus_mV_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t mV = 0;
    Read_24V_Voltage_mV(&mV);

    pack_u16_le(out, (mV > 65535U) ? 65535U : (uint16_t)mV);
    return 2;
}

size_t CAN_Packer_12V_Bus_mV_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t mV = 0;
    Read_12V_Voltage_mV(&mV);

    pack_u16_le(out, (mV > 65535U) ? 65535U : (uint16_t)mV);
    return 2;
}

size_t CAN_Packer_HighSide_Module_Current_mA_2Byte(HighSide_Module_t module,
                                                    uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t mA = 0;
    if (Read_HighSide_Module_Current_mA(module, &mA) != PE_SUCCESS)
        return 0;

    pack_u16_le(out, (mA > 65535U) ? 65535U : (uint16_t)mA);
    return 2;
}

size_t CAN_Packer_24V_Bus_Current_mA_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t mA = 0;
    Read_24V_Bus_Current_mA(&mA);

    pack_u16_le(out, (mA > 65535U) ? 65535U : (uint16_t)mA);
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
