/**
 * @file    power_monitor.c
 * @brief   Bus voltage/current reads and OC + UV protection.
 *
 * @details Current sensing path (TPS2493 IMON pin):
 *            IMON → 10 kΩ → ADC pin → 100 kΩ → GND
 *            V_imon  = V_adc × 110/100      (compensate divider)
 *            I_mA    = V_imon_mV × 1000 / 144
 *                    = V_adc_mV × 1100 / 144
 *
 *          Bus voltage divider: V_bus = V_adc × (200k + 22k) / 22k = V_adc × 222/22.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "power_monitor.h"
#include "adc_driver.h"
#include "hs_switch.h"
#include <stdint.h>

/* UV recovery hysteresis — bus must rise this far above threshold before
 * the UV error bit auto-clears. */
#define UV_RECOVER_HYST_MV  500U

typedef struct {
    uint16_t oc_mA[HS_MODULE_COUNT];   /* per-channel OC trip threshold */
    uint16_t uv_24V_mV;                /* 24 V UV trip threshold       */
    uint16_t uv_12V_mV;                /* 12 V UV trip threshold       */
    uint8_t  err_mask;                 /* current ERR_xxx bitmask      */
} PM_State_t;

static PM_State_t pm = {
    .oc_mA = {
        [HS_MODULE_DRIVE]     = OC_THRESHOLD_DEFAULT_MA,
        [HS_MODULE_EXTRUDER]  = OC_THRESHOLD_DEFAULT_MA,
        [HS_MODULE_SCRUBBING] = OC_THRESHOLD_DEFAULT_MA,
    },
    .uv_24V_mV = UV_24V_THRESHOLD_DEFAULT,
    .uv_12V_mV = UV_12V_THRESHOLD_DEFAULT,
    .err_mask  = ERR_NONE,
};

/* ────────── Threshold setters ────────── */

void PM_Set_OC_Threshold(HS_Module_t module, uint16_t threshold_mA)
{
    if (module < HS_MODULE_COUNT) pm.oc_mA[module] = threshold_mA;
}

void PM_Set_UV_24V_Threshold(uint16_t threshold_mV) { pm.uv_24V_mV = threshold_mV; }
void PM_Set_UV_12V_Threshold(uint16_t threshold_mV) { pm.uv_12V_mV = threshold_mV; }

uint16_t PM_Get_OC_Threshold(HS_Module_t module)
{
    return (module < HS_MODULE_COUNT) ? pm.oc_mA[module] : 0U;
}

uint16_t PM_Get_UV_24V_Threshold(void) { return pm.uv_24V_mV; }
uint16_t PM_Get_UV_12V_Threshold(void) { return pm.uv_12V_mV; }

/* ────────── Live readings ────────── */

static inline uint32_t mV_to_current_mA(uint32_t v_adc_mV)
{
    /* 1100 = 110/100 (divider comp) × 1000 (mA conversion) */
    return (v_adc_mV * 1100U + 72U) / 144U;
}

uint32_t PM_Read_HS_Current_mA(HS_Module_t module)
{
    uint16_t adc;
    switch (module) {
    case HS_MODULE_DRIVE:     adc = ADC_VAL[CURR_MON_1]; break;
    case HS_MODULE_EXTRUDER:  adc = ADC_VAL[CURR_MON_2]; break;
    case HS_MODULE_SCRUBBING: adc = ADC_VAL[CURR_MON_3]; break;
    default: return 0U;
    }
    return mV_to_current_mA(adc_to_mV(adc));
}

uint32_t PM_Read_24V_Bus_Current_mA(void)
{
    return mV_to_current_mA(adc_to_mV(ADC_VAL[CURR_MON_24V]));
}

uint32_t PM_Read_24V_Voltage_mV(void)
{
    return adc_to_mV(ADC_VAL[VADC_24]) * 222U / 22U;
}

uint32_t PM_Read_12V_Voltage_mV(void)
{
    return adc_to_mV(ADC_VAL[VADC_12]) * 222U / 22U;
}

/* ────────── Protection runtime ────────── */

uint8_t PM_RunProtection(void)
{
    /* ---- OC: trip and disable any HS channel exceeding its threshold ---- */
    static const uint8_t oc_bit[HS_MODULE_COUNT] = {
        [HS_MODULE_DRIVE]     = ERR_OC_DRIVE,
        [HS_MODULE_EXTRUDER]  = ERR_OC_EXTRUDER,
        [HS_MODULE_SCRUBBING] = ERR_OC_SCRUBBING,
    };

    for (int i = 0; i < HS_MODULE_COUNT; i++) {
        if (!HS_IsEnabled((HS_Module_t)i)) continue;

        uint32_t i_mA = PM_Read_HS_Current_mA((HS_Module_t)i);
        if (i_mA > pm.oc_mA[i]) {
            HS_Disable((HS_Module_t)i);
            pm.err_mask |= oc_bit[i];
        }
    }

    /* ---- UV: latch bit while below threshold, clear with hysteresis ---- */
    uint32_t v24 = PM_Read_24V_Voltage_mV();
    uint32_t v12 = PM_Read_12V_Voltage_mV();

    if (v24 < pm.uv_24V_mV) {
        pm.err_mask |= ERR_UV_24V;
    } else if (v24 >= (uint32_t)pm.uv_24V_mV + UV_RECOVER_HYST_MV) {
        pm.err_mask &= (uint8_t)~ERR_UV_24V;
    }

    if (v12 < pm.uv_12V_mV) {
        pm.err_mask |= ERR_UV_12V;
    } else if (v12 >= (uint32_t)pm.uv_12V_mV + UV_RECOVER_HYST_MV) {
        pm.err_mask &= (uint8_t)~ERR_UV_12V;
    }

    return pm.err_mask;
}

uint8_t PM_GetErrorMask(void)
{
    return pm.err_mask;
}

void PM_Clear_OC_Error(HS_Module_t module)
{
    switch (module) {
    case HS_MODULE_DRIVE:     pm.err_mask &= (uint8_t)~ERR_OC_DRIVE;     break;
    case HS_MODULE_EXTRUDER:  pm.err_mask &= (uint8_t)~ERR_OC_EXTRUDER;  break;
    case HS_MODULE_SCRUBBING: pm.err_mask &= (uint8_t)~ERR_OC_SCRUBBING; break;
    default: break;
    }
}
