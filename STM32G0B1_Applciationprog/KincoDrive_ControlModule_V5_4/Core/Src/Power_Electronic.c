/**
 * @file    Power_Electronic.c
 * @brief   High-side power control, analog sensing, protection, and CAN packers.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#include "Power_Electronic.h"
#include "CAN_Handler.h"
#include "main.h"
#include "adc.h"
#include "fdcan.h"
#include "eeprom_driver.h"
#include "error_manager.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC DMA buffer (written continuously by hardware)
 * ════════════════════════════════════════════════════════════════════════════ */

volatile uint16_t ADC_VAL[ADC_BUF_LEN];

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC conversion constants
 * ════════════════════════════════════════════════════════════════════════════ */

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

/* ════════════════════════════════════════════════════════════════════════════
 *  High-side power module enable / disable
 * ════════════════════════════════════════════════════════════════════════════ */

int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{
    GPIO_TypeDef *en_port, *pg_port;
    uint16_t      en_pin,   pg_pin;

    switch (module) {
    case HS_MODULE_DRIVE:
        en_port = HS_DR_EN_GPIO_Port;  en_pin = HS_DR_EN_Pin;
        pg_port = HS_DR_PG_GPIO_Port;  pg_pin = HS_DR_PG_Pin;
        break;
    case HS_MODULE_EXTRUDER:
        en_port = HS_E_EN_GPIO_Port;   en_pin = HS_E_EN_Pin;
        pg_port = HS_E_PG_GPIO_Port;   pg_pin = HS_E_PG_Pin;
        break;
    case HS_MODULE_SCRUBBING:
        en_port = HS_SC_EN_GPIO_Port;  en_pin = HS_SC_EN_Pin;
        pg_port = HS_SC_PG_GPIO_Port;  pg_pin = HS_SC_PG_Pin;
        break;
    default:
        return PE_ERR_INVALID_PARAM;
    }

    /* Assert enable pin */
    HAL_GPIO_WritePin(en_port, en_pin, GPIO_PIN_SET);

    /* Wait for power-good (active low) */
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(pg_port, pg_pin) != GPIO_PIN_RESET) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            HAL_GPIO_WritePin(en_port, en_pin, GPIO_PIN_RESET);
            return PE_ERR_TIMEOUT;
        }
        HAL_Delay(1);
    }

    return PE_SUCCESS;
}

int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{
    (void)timeout_ms;   /* not used for disable, kept for API symmetry */

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

int Enable_HighSide_Power_All(uint32_t timeout_ms)
{
    int rc;
    rc = Enable_HighSide_Power_Module(HS_MODULE_DRIVE, timeout_ms);
    if (rc != PE_SUCCESS) return rc;

    rc = Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, timeout_ms);
    if (rc != PE_SUCCESS) return rc;

    return Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, timeout_ms);
}

int Disable_HighSide_Power_All(uint32_t timeout_ms)
{
    int rc;
    rc = Disable_HighSide_Power_Module(HS_MODULE_DRIVE, timeout_ms);
    if (rc != PE_SUCCESS) return rc;

    rc = Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, timeout_ms);
    if (rc != PE_SUCCESS) return rc;

    return Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, timeout_ms);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC calibration and DMA start
 * ════════════════════════════════════════════════════════════════════════════ */

int Calibrate_ADC1(void)
{
    HAL_ADC_Stop(&hadc1);   /* must be stopped before calibration */

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
        return PE_ERR_GEN;
    }
    return PE_SUCCESS;
}

int Start_ADC1_DMA(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADC_VAL, ADC_BUF_LEN) != HAL_OK) {
        return PE_ERR_GEN;
    }
    return PE_SUCCESS;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Current and voltage readings
 * ════════════════════════════════════════════════════════════════════════════ */

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

/* ════════════════════════════════════════════════════════════════════════════
 *  Thermistor temperature reading (Steinhart-Hart B-parameter equation)
 * ════════════════════════════════════════════════════════════════════════════ */

int Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, uint32_t *temperature_C)
{
    if (thermistor < TEMP_PTC_1 || thermistor > TEMP_PTC_6) {
        return PE_ERR_INVALID_PARAM;
    }

    uint16_t adc_value = ADC_VAL[thermistor];

    if (adc_value == 0) {
        return PE_ERR_GEN;  /* avoid division by zero */
    }

    float voltage = (adc_value / 4095.0f) * 3.3f;

    /*
     * Circuit: 3.3V --- R_pull(100k) --- ADC node --- R_therm(NTC) --- GND
     * R_therm = R_pull * V_out / (V_ref - V_out)
     */
    const float R_PULL = 100000.0f;
    float resistance = R_PULL * voltage / (3.3f - voltage);

    /* Steinhart-Hart simplified B-parameter equation:
     *   1/T = 1/T0 + (1/B) * ln(R/R0)
     */
    const float R0 = 100000.0f;     /* resistance at 25 degC */
    const float T0 = 298.15f;       /* 25 degC in Kelvin */
    const float B  = 3950.0f;       /* B-constant (update to match your thermistor) */

    float ln_ratio = logf(resistance / R0);
    float inv_T    = (1.0f / T0) + (1.0f / B) * ln_ratio;

    if (inv_T <= 0.0f) {
        return PE_ERR_GEN;
    }

    float temp_C = (1.0f / inv_T) - 273.15f;

    /* Reject obviously wrong readings */
    if (temp_C < -40.0f || temp_C > 200.0f) {
        return PE_ERR_GEN;
    }

    *temperature_C = (uint32_t)temp_C;
    return PE_SUCCESS;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  12V buck converter
 * ════════════════════════════════════════════════════════════════════════════ */

int Enable_12V_Buck_Converter(void)
{
    uint32_t bus_24V_1DP = 0;
    Read_24V_Voltage_1DP(&bus_24V_1DP);

    const uint32_t MIN_24V_FOR_12V = 200; /* 20.0V minimum */
    if (bus_24V_1DP < MIN_24V_FOR_12V) {
        return PE_ERR_GEN;
    }

    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_SET);
    return PE_SUCCESS;
}

void Disable_12V_Buck_Converter(void)
{
    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_RESET);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Overcurrent / Overvoltage shutdown protection
 * ════════════════════════════════════════════════════════════════════════════ */

static uint8_t overcurrent_state[3] = {0, 0, 0};   /* per-module: 0=OK, 1=OC */
static uint8_t overvoltage_state    = 0;            /* 0=OK, 1=soft, 2=hard */

void Shutdown_Protection(void)
{
    static const HighSide_Module_t modules[3] = {
        HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING
    };
    static const ErrorSource_t oc_src[3] = {
        ERR_SRC_OVERCURRENT_DRIVE, ERR_SRC_OVERCURRENT_EXT, ERR_SRC_OVERCURRENT_SCRUB
    };

    /* Get config thresholds */
    const Config *cfgp = EEPROM_GetCachedConfig();
    Config cfg_local;
    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &cfg_local);
        if (checkcfg(&cfg_local)) {
            cfgp = &cfg_local;
        } else {
            return;     /* no valid config — skip protection check */
        }
    }

    /* ── Per-module overcurrent check ── */
    for (int i = 0; i < 3; ++i) {
        uint32_t current_mA = 0;
        if (Read_HighSide_Module_Current_mA(modules[i], &current_mA) != PE_SUCCESS) {
            continue;
        }

        uint8_t new_oc = (current_mA >= (uint32_t)cfgp->over_current) ? 1 : 0;

        if (new_oc && !overcurrent_state[i]) {
            /* Transition OK → overcurrent: shut down this module */
            Disable_HighSide_Power_Module(modules[i], 500);
            overcurrent_state[i] = 1;
            Error_Manager_Log(oc_src[i], ERR_SEV_CRITICAL, (uint16_t)current_mA);
            send_ack(CAN_ID_ACK_GENERAL, 0x11);
        } else if (!new_oc && overcurrent_state[i]) {
            overcurrent_state[i] = 0;
        }
    }

    /* ── 24V bus overvoltage check ── */
    uint32_t bus_1dp = 0;
    Read_24V_Voltage_1DP(&bus_1dp);
    uint32_t bus_mV = bus_1dp * 100U;

    uint8_t new_ov = 0;
    if (bus_mV >= (uint32_t)cfgp->hard_over_voltage) {
        new_ov = 2;     /* hard overvoltage */
    } else if (bus_mV >= (uint32_t)cfgp->soft_over_voltage) {
        new_ov = 1;     /* soft overvoltage */
    }

    if (new_ov == 2 && overvoltage_state != 2) {
        Disable_HighSide_Power_All(500);
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_HARD, ERR_SEV_CRITICAL, (uint16_t)bus_mV);
        send_ack(CAN_ID_ACK_GENERAL, 0x12);
    }

    if (new_ov == 1 && overvoltage_state != 1) {
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_SOFT, ERR_SEV_WARNING, (uint16_t)bus_mV);
    }

    overvoltage_state = new_ov;
}

void Shutdown_Protection_ResetState(void)
{
    overvoltage_state = 0;
    memset(overcurrent_state, 0, sizeof(overcurrent_state));
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN command handlers — high-side power on/off
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Generic handler for a single high-side module power command.
 *         params[0] != 0 → enable, params[0] == 0 → disable.
 */
static void HS_Power_Command(HighSide_Module_t module, uint32_t id,
                             uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return;
    }

    if (params[0] != 0) {
        if (!Error_Manager_IsPowerAllowed()) {
            send_nack(CAN_ID_ACK_GENERAL, (uint8_t)CAN_MSG_TYPE(id));
            return;
        }
        Enable_HighSide_Power_Module(module, 500);
    } else {
        Disable_HighSide_Power_Module(module, 500);
    }

    send_ack(CAN_ID_ACK_GENERAL, (uint8_t)CAN_MSG_TYPE(id));
}

void CAN_Handler_Set_HS_Drive_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    HS_Power_Command(HS_MODULE_DRIVE, id, params, len);
}

void CAN_Handler_Set_HS_Extruder_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    HS_Power_Command(HS_MODULE_EXTRUDER, id, params, len);
}

void CAN_Handler_Set_HS_Scrubbing_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    HS_Power_Command(HS_MODULE_SCRUBBING, id, params, len);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN telemetry data packers
 * ════════════════════════════════════════════════════════════════════════════ */

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
    if (Read_HighSide_Module_Current_mA(module, &current_mA) != PE_SUCCESS) {
        return 0;
    }

    /* mA → tenths of ampere (0.1A), rounded to nearest */
    uint32_t current_1dp = (current_mA + 50U) / 100U;
    if (current_1dp > 300U) current_1dp = 300U;  /* 30.0A cap */

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

    uint32_t temperature_C = 0;
    if (Read_Thermistor_Temperature_C(thermistor, &temperature_C) != PE_SUCCESS) {
        return 0;
    }

    /* Offset encoding: wire = degC + 40  (so -40 degC → 0x00) */
    int32_t encoded = (int32_t)temperature_C + 40;
    if (encoded < 0)   encoded = 0;
    if (encoded > 255)  encoded = 255;

    out[0] = (uint8_t)encoded;
    return 1;
}

size_t CAN_Packer_Protection_State_1Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    /*
     * Bit layout:
     *   [1:0] drive overcurrent      (0=OK, 1=OC)
     *   [3:2] extruder overcurrent
     *   [5:4] scrubbing overcurrent
     *   [7:6] 24V bus overvoltage     (0=OK, 1=soft, 2=hard)
     */
    uint8_t packed = 0;
    packed |=  (overcurrent_state[0] & 0x03);
    packed |= ((overcurrent_state[1] & 0x03) << 2);
    packed |= ((overcurrent_state[2] & 0x03) << 4);
    packed |= ((overvoltage_state    & 0x03) << 6);

    out[0] = packed;
    return 1;
}
