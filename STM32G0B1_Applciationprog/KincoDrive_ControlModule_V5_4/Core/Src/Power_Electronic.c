#include "Power_Electronic.h"
#include "CAN_Handler.h"
#include "main.h"
#include "adc.h"
#include <stdint.h>
#include <string.h>
#include "stm32g0xx.h"
#include "fdcan.h"
#include <math.h>

#include "eeprom_driver.h"
#include "error_manager.h"

static volatile uint16_t ADC_VAL[ADC_BUF_LEN];

/*🔴Preserve*/
int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    switch (module)
    {
    case HS_MODULE_DRIVE:
        HAL_GPIO_WritePin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin, GPIO_PIN_SET);
        
        while (HAL_GPIO_ReadPin(HS_DR_PG_GPIO_Port, HS_DR_PG_Pin) != GPIO_PIN_RESET)
        {
            if ((HAL_GetTick() - start) >= timeout_ms)
            {
                HAL_GPIO_WritePin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin, GPIO_PIN_RESET);
                return PE_ERR_TIMEOUT; 
            }
            HAL_Delay(1);
        }

        break;

    case HS_MODULE_EXTRUDER:
        HAL_GPIO_WritePin(HS_E_EN_GPIO_Port, HS_E_EN_Pin, GPIO_PIN_SET);
        
        while (HAL_GPIO_ReadPin(HS_E_PG_GPIO_Port, HS_E_PG_Pin) != GPIO_PIN_RESET)
        {
            if ((HAL_GetTick() - start) >= timeout_ms)
            {
                HAL_GPIO_WritePin(HS_E_EN_GPIO_Port, HS_E_EN_Pin, GPIO_PIN_RESET);
                return PE_ERR_TIMEOUT; 
            }
            HAL_Delay(1);
        }
        break;

    case HS_MODULE_SCRUBBING:
        HAL_GPIO_WritePin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin, GPIO_PIN_SET);
        
        while (HAL_GPIO_ReadPin(HS_SC_PG_GPIO_Port, HS_SC_PG_Pin) != GPIO_PIN_RESET)
        {
            if ((HAL_GetTick() - start) >= timeout_ms)
            {
                HAL_GPIO_WritePin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin, GPIO_PIN_RESET);
                return PE_ERR_TIMEOUT; 
            }
            HAL_Delay(1);
        }
        break;

    default:
        return PE_ERR_INVALID_PARAM;
    }

    return PE_SUCCESS;
}

/*🔴Preserve*/
int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms)
{


    switch (module)
    {
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
    if (rc != 0) return rc;

    rc = Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, timeout_ms);
    if (rc != 0) return rc;

    rc = Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, timeout_ms);
    return rc;
}


int Disable_HighSide_Power_All(uint32_t timeout_ms)
{
    int rc;

    rc = Disable_HighSide_Power_Module(HS_MODULE_DRIVE, timeout_ms);
    if (rc != 0) return rc;

    rc = Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, timeout_ms);
    if (rc != 0) return rc;

    rc = Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, timeout_ms);
    return rc;
}

/*🔴Preserve*/
int      Calibrate_ADC1(void)
{
    /* Ensure ADC is stopped before calibration. Ignore stop return (may already be stopped). */
    HAL_ADC_Stop(&hadc1);

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        return PE_ERR_GEN;
    }
    else
    {
        return PE_SUCCESS;
    }
}

/*🔴Preserve*/
int Start_ADC1_DMA(void)
{
    // Start ADC1 in DMA mode to read multiple channels
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADC_VAL, ADC_BUF_LEN) != HAL_OK)
    {
        return PE_ERR_GEN;
    }
    else
    {
        return PE_SUCCESS;
    }
}

/*🔴Preserve*/
int Read_HighSide_Module_Current_mA(HighSide_Module_t module, uint32_t *current_mA)
{
    uint16_t adc_value;
    float voltage;
    float current;

    switch (module)
    {
    case HS_MODULE_DRIVE:
        adc_value = ADC_VAL[CURR_MON_1];
        break;
    case HS_MODULE_EXTRUDER:
        adc_value = ADC_VAL[CURR_MON_2];
        break;
    case HS_MODULE_SCRUBBING:
        adc_value = ADC_VAL[CURR_MON_3];
        break;
    default:
        return PE_ERR_INVALID_PARAM; // invalid module
    }

    // Convert ADC value to voltage (assuming 12-bit ADC and 3.3V reference)
    voltage = (adc_value / 4095.0f) * 3.3f;

    // Convert voltage to current, TPS2493 current sense factor is 48 * 0.003(Rsense) = 0.144
    current = voltage / 0.144f; 

    // Convert current to milliamperes
    *current_mA = (uint32_t)(current * 1000.0f);

    return PE_SUCCESS;
}

/*🔴Preserve*/
void Read_24V_Bus_Current_mA(uint32_t *current_mA)
{
    uint16_t adc_value = ADC_VAL[CURR_MON_24V];

    // Convert ADC value to voltage (assuming 12-bit ADC and 3.3V reference)
    float voltage = (adc_value / 4095.0f) * 3.3f;

    // Convert voltage to current, TPS2493 current sense factor is 48 * 0.003(Rsense) = 0.144
    float current = voltage / 0.144f;

    // Convert current to milliamperes
    *current_mA = (uint32_t)(current * 1000.0f);
}


#define VOLTAGE_DIVIDER_RATIO (222.0f / 22.0f) // (R1 + R2) / R2
#define CORRECTION_FACTOR_DP1 10.0f //to convert to 1 decimal place (e.g., 24.5V -> 245)

/*🔴Preserve*/
void Read_24V_Voltage_1DP(uint32_t *voltage_1DP)
{
    uint16_t adc_value = ADC_VAL[VADC_24];
    float voltage;

    // Convert ADC value to voltage 
    voltage = (adc_value / 4095.0f) * 3.3f;

    // Considering voltage divider for 24V measurement
    voltage = voltage * VOLTAGE_DIVIDER_RATIO * CORRECTION_FACTOR_DP1;

    *voltage_1DP = (uint32_t)voltage;
}

/*🔴Preserve*/
void Read_12V_Voltage_1DP(uint32_t *voltage_1DP)
{
    uint16_t adc_value = ADC_VAL[VADC_12];
    float voltage;

    // Convert ADC value to voltage
    voltage = (adc_value / 4095.0f) * 3.3f;

    // Considering voltage divider for 12V measurement
    voltage = voltage * VOLTAGE_DIVIDER_RATIO * CORRECTION_FACTOR_DP1;

    *voltage_1DP = (uint32_t)voltage;

}

/*🔴Preserve*/
int Enable_12V_Buck_Converter(void)
{
    uint32_t Bus_24V_1DP = 0;

    Read_24V_Voltage_1DP((uint32_t *)&Bus_24V_1DP);

    const uint32_t min_24V_for_12V = 200; // 20V in 1DP

    if (Bus_24V_1DP < min_24V_for_12V)
    {
        return PE_ERR_GEN; // Not enough 24V bus voltage
    }

    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_SET);

    return PE_SUCCESS;
}

/*🔴Preserve*/
void Disable_12V_Buck_Converter(void)
{
    HAL_GPIO_WritePin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin, GPIO_PIN_RESET);
}

//🟡Adapt: use already tested thermistor code, but use the ADC buffer order
int Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, uint32_t *temperature_C)
{
    if (thermistor >= TEMP_PTC_1 && thermistor <= TEMP_PTC_6)
    {
        uint16_t adc_value = ADC_VAL[thermistor];

        /* Avoid division by zero if ADC reads 0 */
        if (adc_value == 0) {
            return PE_ERR_GEN;
        }

        float voltage = (adc_value / 4095.0f) * 3.3f;

        /* 100kΩ pull-up to 3.3V, thermistor to GND:
         *   V_out = V_ref * R_therm / (R_pull + R_therm)
         *   => R_therm = R_pull * V_out / (V_ref - V_out)
         *
         * For PTC (resistance increases with temperature):
         *   If thermistor is on the high side (to 3.3V) and pull-down to GND:
         *   => R_therm = R_pull * (V_ref - V_out) / V_out
         *
         * Adjust the formula below to match your circuit topology.
         * Assuming: 3.3V --- R_pull(100k) --- ADC_node --- R_therm(PTC) --- GND
         */
        const float R_PULL = 100000.0f;  /* 100kΩ pull-up resistor */
        float resistance = R_PULL * voltage / (3.3f - voltage);

        /* Steinhart-Hart (simplified B-parameter equation):
         *   1/T = 1/T0 + (1/B) * ln(R/R0)
         *
         * Adjust B and R0/T0 to match your PTC thermistor datasheet.
         */
        const float R0 = 100000.0f;  /* resistance at reference temperature T0 */
        const float T0 = 298.15f;    /* reference temperature in Kelvin (25°C) */
        const float B  = 3950.0f;    /* B-constant from datasheet — CHANGE THIS */

        /* For a PTC: resistance increases with temperature.
         * The B-parameter equation still applies but the sign of ln(R/R0)
         * naturally handles the direction. Verify with your datasheet. */
        float ln_ratio = logf(resistance / R0);
        float inv_T = (1.0f / T0) + (1.0f / B) * ln_ratio;

        if (inv_T <= 0.0f) {
            return PE_ERR_GEN; /* mathematically invalid */
        }

        float temp_K = 1.0f / inv_T;
        float temp_C = temp_K - 273.15f;

        /* Sanity clamp: reject clearly wrong readings */
        if (temp_C < -40.0f || temp_C > 200.0f) {
            return PE_ERR_GEN;
        }

        *temperature_C = (uint32_t)temp_C;
        return PE_SUCCESS;
    }
    else
    {
        return PE_ERR_INVALID_PARAM;
    }
}


/*
 * Module overcurrent state (per module):  0 = OK, 1 = overcurrent
 * CAN ID 0x114: payload[0] = module index, payload[1] = state
 * Transmits only on state change.
 */
static uint8_t overcurrent_state[3] = {0, 0, 0};

/*
 * Bus overvoltage state (shared across all modules):
 *   0 = OK, 1 = soft overvoltage, 2 = hard overvoltage
 * CAN ID 0x115: payload[0] = state
 * Transmits only on state change.
 * Hard overvoltage shuts down all 3 modules.
 */
static uint8_t overvoltage_state = 0;

/*
🟡Adapt:Demo for how shutdown can be done. can rewrite for specific shutdown behaviour linked to software
*/
void Shutdown_Protection(void)
{
    HighSide_Module_t modules[] = { HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING };

    /* Get config (cached preferred) */
    const Config *cfgp = EEPROM_GetCachedConfig();
    Config cfg_local;
    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &cfg_local);
        if (checkcfg(&cfg_local)) {
            cfgp = &cfg_local;
        } else {
            return; /* no valid config — cannot check thresholds */
        }
    }

    /* ── Overcurrent check (per module) ── */
    for (int i = 0; i < 3; ++i) {
        uint32_t current_mA = 0;
        if (Read_HighSide_Module_Current_mA(modules[i], &current_mA) != PE_SUCCESS) {
            continue; /* skip module if ADC read fails */
        }

        uint8_t new_oc = (current_mA >= (uint32_t)cfgp->over_current) ? 1 : 0;

        if (new_oc && !overcurrent_state[i]) {
            /* transition OK → overcurrent: shutdown that module */
            Disable_HighSide_Power_Module(modules[i], 500);
            overcurrent_state[i] = 1;

            /* Log critical error per module */
            ErrorSource_t oc_src[] = { ERR_SRC_OVERCURRENT_DRIVE, ERR_SRC_OVERCURRENT_EXT, ERR_SRC_OVERCURRENT_SCRUB };
            Error_Manager_Log(oc_src[i], ERR_SEV_CRITICAL, (uint16_t)current_mA);

            send_ack(0x700, 0x11); /* send alert on OC */
        } else if (!new_oc && overcurrent_state[i]) {
            overcurrent_state[i] = 0;
        }

    }

    /* ── Overvoltage check (24V bus, applies to all modules) ── */
    uint32_t bus_1dp = 0;
    Read_24V_Voltage_1DP(&bus_1dp);
    uint32_t bus_mV = bus_1dp * 100U;

    uint8_t new_ov = 0;
    if (bus_mV >= (uint32_t)cfgp->hard_over_voltage) {
        new_ov = 2; /* hard */
    } else if (bus_mV >= (uint32_t)cfgp->soft_over_voltage) {
        new_ov = 1; /* soft */
    }

    /* hard overvoltage: shutdown all modules */
    if (new_ov == 2 && overvoltage_state != 2) {
        Disable_HighSide_Power_All(500);
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_HARD, ERR_SEV_CRITICAL, (uint16_t)bus_mV);
        send_ack(0x700, 0x12); /* send alert on hard OV */
    }

    /* soft overvoltage: warning only */
    if (new_ov == 1 && overvoltage_state != 1) {
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_SOFT, ERR_SEV_WARNING, (uint16_t)bus_mV);
    }

    overvoltage_state = new_ov;
}

/*🟡Adapt*/
void Shutdown_Protection_ResetState(void)
{
    overvoltage_state = 0;
    memset(overcurrent_state, 0, sizeof(overcurrent_state));
}

void CAN_Handler_Set_HS_Drive_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return; /* invalid length */
    }

    uint8_t power_on = params[0] != 0;

    if (power_on) {
        if (!Error_Manager_IsPowerAllowed()) {
            send_nack(0x700, (uint8_t)id); /* blocked by error state */
            return;
        }
        Enable_HighSide_Power_Module(HS_MODULE_DRIVE, 500);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_DRIVE, 500);
    }

    send_ack(0x700, id);
}

void CAN_Handler_Set_HS_Extruder_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return; /* invalid length */
    }

    uint8_t power_on = params[0] != 0;

    if (power_on) {
        if (!Error_Manager_IsPowerAllowed()) {
            send_nack(0x700, (uint8_t)id); /* blocked by error state */
            return;
        }
        Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 500);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 500);
    }

    send_ack(0x700, id);

}

void CAN_Handler_Set_HS_Scrubbing_Power(uint32_t id, uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return; /* invalid length */
    }

    uint8_t power_on = params[0] != 0;

    if (power_on) {
        if (!Error_Manager_IsPowerAllowed()) {
            send_nack(0x700, (uint8_t)id); /* blocked by error state */
            return;
        }
        Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 500);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 500);
    }

    send_ack(0x700, id);
}




size_t CAN_Packer_24V_Bus_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t v_1DP = 0;
    Read_24V_Voltage_1DP(&v_1DP);

    uint16_t val = (v_1DP > 65535U) ? 65535U : (uint16_t)v_1DP;
    
    /* Little-endian */
    out[0] = (uint8_t)(val & 0xFF);
    out[1] = (uint8_t)((val >> 8) & 0xFF);
    return 2;
}


size_t CAN_Packer_12V_Bus_1Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint32_t v_1DP = 0;
    /* If your Read_12V_Voltage_mV returns int status, adjust the call/check accordingly.
       Here we assume it fills vm_mV and returns PE_SUCCESS on success. */

    Read_12V_Voltage_1DP(&v_1DP); /* fill */



    if (v_1DP < 0) v_1DP = 0;


    uint8_t send = (v_1DP > 255) ? 255U : (uint8_t)v_1DP;
    out[0] = send;
    return 1;
}


size_t CAN_Packer_HighSide_Module_Current_1DP_1Byte(HighSide_Module_t module, uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint32_t current_mA = 0;
    if (Read_HighSide_Module_Current_mA(module, &current_mA) != PE_SUCCESS) {
        return 0;
    }

    /* Convert mA -> tenths of ampere (0.1 A units).
       Use rounding to nearest 0.1 A: add 50 mA before dividing by 100. */
    uint32_t current_1dp = (current_mA + 50U) / 100U;

    /* Logical safety cap (30.0 A -> 300 in 0.1A units) */
    if (current_1dp > 300U) current_1dp = 300U;

    /* Clamp to one byte for CAN payload; 255 means ">=255" */
    uint8_t send = (current_1dp > 255U) ? 255U : (uint8_t)current_1dp;
    out[0] = send;
    return 1;
}


size_t CAN_Packer_24V_Bus_Current_1DP_2Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 2) return 0;

    uint32_t current_mA = 0;
    Read_24V_Bus_Current_mA(&current_mA);

    /* Convert mA → tenths of ampere (0.1A units), rounded */
    uint16_t current_1dp = (uint16_t)((current_mA + 50U) / 100U);

    /* Little-endian */
    out[0] = (uint8_t)(current_1dp & 0xFF);
    out[1] = (uint8_t)((current_1dp >> 8) & 0xFF);

    return 2;
}

size_t CAN_Packer_Thermistor_Temp_1Byte(ADC_Peripheral_t thermistor, uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint32_t temperature_C = 0;
    if (Read_Thermistor_Temperature_C(thermistor, &temperature_C) != PE_SUCCESS) {
        return 0;
    }

    /* Offset encoding: add 40 so -40 °C maps to 0x00, 0 °C maps to 0x28 (40).
     * Read_Thermistor_Temperature_C returns uint32_t (already clamped to -40..200
     * internally via cast to uint32_t, so negatives wrap — guard here). */
    int32_t temp_signed = (int32_t)temperature_C;
    int32_t encoded = temp_signed + 40;

    if (encoded < 0)   encoded = 0;
    if (encoded > 255)  encoded = 255;

    out[0] = (uint8_t)encoded;
    return 1;
}


size_t CAN_Packer_Protection_State_1Byte(uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint8_t packed = 0;

    /* Bits [1:0] — drive overcurrent (clamp to 2-bit max) */
    packed |= (overcurrent_state[0] & 0x03);

    /* Bits [3:2] — extruder overcurrent */
    packed |= (overcurrent_state[1] & 0x03) << 2;

    /* Bits [5:4] — scrubbing overcurrent */
    packed |= (overcurrent_state[2] & 0x03) << 4;

    /* Bits [7:6] — 24V bus overvoltage */
    packed |= (overvoltage_state & 0x03) << 6;

    out[0] = packed;
    return 1;
}