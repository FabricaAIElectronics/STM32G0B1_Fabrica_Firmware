/**
 * @file    battery.c
 * @brief   6S Lithium SOC estimator (OCV lookup + IR compensation).
 *
 * @author  jordan
 * @date    2026-05-07
 */

#include "battery.h"

/* ============================================================
 * Runtime SOC-low warning threshold.
 *   Initialised to BATTERY_LOW_SOC_PCT (15) at boot.
 *   Overridden by Battery_SetLowSocThreshold_pct() — typically called
 *   from Apply_Config() after EEPROM load and from the CMD_BAT_CFG
 *   (0x147) handler.
 *   A value of 0 disables the warning entirely.
 * ============================================================ */
static uint8_t s_low_soc_threshold = BATTERY_LOW_SOC_PCT;

void Battery_SetLowSocThreshold_pct(uint8_t pct)
{
    s_low_soc_threshold = (pct > 100U) ? 100U : pct;
}

uint8_t Battery_GetLowSocThreshold_pct(void)
{
    return s_low_soc_threshold;
}

bool Battery_IsLow(uint8_t soc_pct)
{
    /* 0 threshold = warning disabled. */
    return (s_low_soc_threshold > 0U) && (soc_pct < s_low_soc_threshold);
}

/* ============================================================
 * 6S Li-ion / Li-Po SOC vs OCV (open-circuit pack voltage in mV).
 *
 *   per cell × 6 cells = pack mV
 *   4.20 V × 6 = 25 200 mV → 100 %
 *   3.85 V × 6 = 23 100 mV →  50 %
 *   3.27 V × 6 = 19 600 mV →   0 %  (BATTERY_CUTOFF_MV)
 *
 * The curve is non-linear — we sample it at 11 points and interpolate
 * linearly between adjacent points. Entries MUST be sorted descending by
 * voltage (Battery_EstimateSOC_pct relies on this).
 * ============================================================ */
typedef struct {
    uint16_t mV;       /* pack OCV in mV     */
    uint8_t  soc_pct;  /* corresponding SOC  */
} soc_point_t;

static const soc_point_t soc_curve[] = {
    {25200, 100},   /* 4.20 V/cell — fully charged */
    {24600,  90},   /* 4.10 V/cell                  */
    {24000,  80},   /* 4.00 V/cell                  */
    {23700,  70},   /* 3.95 V/cell                  */
    {23400,  60},   /* 3.90 V/cell                  */
    {23100,  50},   /* 3.85 V/cell — nominal       */
    {22800,  40},   /* 3.80 V/cell                  */
    {22500,  30},   /* 3.75 V/cell                  */
    {22200,  20},   /* 3.70 V/cell                  */
    {21600,  10},   /* 3.60 V/cell                  */
    {20400,   5},   /* 3.40 V/cell — knee           */
    {19600,   0},   /* 3.27 V/cell — cutoff         */
};

#define SOC_CURVE_LEN  (sizeof(soc_curve) / sizeof(soc_curve[0]))

/* ============================================================
 * Battery_EstimateSOC_pct
 *  1. IR compensation: V_OCV = V_meas + I × R_int   (mV)
 *  2. Look up V_OCV in soc_curve, interpolating linearly between the
 *     two bracketing points.
 *  3. Saturate to 0..100 outside the curve range.
 * ============================================================ */
uint8_t Battery_EstimateSOC_pct(uint16_t v24_mV, uint16_t i_bat_mA)
{
    /* IR compensation: V_drop_mV = I_mA × R_milli / 1000 */
    uint32_t v_ocv = (uint32_t)v24_mV
                   + ((uint32_t)i_bat_mA * BATTERY_INT_R_MILLIOHM) / 1000U;

    /* Clamp above-full / below-cutoff cases. */
    if (v_ocv >= soc_curve[0].mV)                  return 100U;
    if (v_ocv <= soc_curve[SOC_CURVE_LEN - 1U].mV) return 0U;

    /* Find the bracketing pair (curve is descending in voltage). */
    for (uint32_t i = 0; i < SOC_CURVE_LEN - 1U; i++) {
        uint16_t v_high = soc_curve[i].mV;
        uint16_t v_low  = soc_curve[i + 1U].mV;

        if (v_ocv <= v_high && v_ocv >= v_low) {
            uint8_t  soc_high = soc_curve[i].soc_pct;
            uint8_t  soc_low  = soc_curve[i + 1U].soc_pct;

            /* Linear interpolation:
             *   soc = soc_low + (v_ocv - v_low) × (soc_high - soc_low)
             *                                    / (v_high - v_low) */
            uint32_t num = (uint32_t)(v_ocv - v_low) * (uint32_t)(soc_high - soc_low);
            uint32_t den = (uint32_t)(v_high - v_low);
            uint32_t soc = (uint32_t)soc_low + (num / den);

            if (soc > 100U) soc = 100U;
            return (uint8_t)soc;
        }
    }

    return 0U;  /* unreachable given the clamps above */
}

/* ============================================================
 * Battery_FilterSOC_pct
 *
 *   Stage 1 — exponential moving average on raw SOC:
 *               ema += (raw - ema) >> SHIFT
 *             stored in 8.8 fixed-point so the rounding error stays small.
 *
 *   Stage 2 — asymmetric hysteresis on the "displayed" value:
 *               drop by 1 % when ema is ≥ DROP below display
 *               rise by 1 % when ema is ≥ RISE above display
 *             RISE > DROP means a brief load-release rebound (which makes
 *             V24 jump) won't drag SOC upward unless the recovery is
 *             sustained for many samples.
 *
 *   First call: snap directly to the raw value so boot SOC is correct.
 * ============================================================ */
uint8_t Battery_FilterSOC_pct(uint8_t raw_soc_pct)
{
    static uint8_t  initialised = 0;
    static uint16_t ema_q8      = 0;     /* raw SOC × 256 */
    static uint8_t  displayed   = 0;

    /* First-call snap — also handles power-on reset cleanly. */
    if (!initialised) {
        ema_q8      = (uint16_t)raw_soc_pct << 8;
        displayed   = raw_soc_pct;
        initialised = 1U;
        return displayed;
    }

    /* Stage 1: EMA on the raw value (8.8 fixed-point). */
    int32_t target_q8 = (int32_t)raw_soc_pct << 8;
    int32_t err_q8    = target_q8 - (int32_t)ema_q8;
    ema_q8           += (int16_t)(err_q8 >> BATTERY_SOC_EMA_SHIFT);

    uint8_t ema_pct = (uint8_t)(ema_q8 >> 8);

    /* Stage 2: ±1 % step toward EMA when delta exceeds asymmetric thresholds. */
    int delta = (int)ema_pct - (int)displayed;

    if (delta <= -(int)BATTERY_SOC_HYST_DROP_PCT) {
        if (displayed > 0U) displayed--;
    }
    else if (delta >= (int)BATTERY_SOC_HYST_RISE_PCT) {
        if (displayed < 100U) displayed++;
    }
    /* else: hold steady — the user sees a stable number. */

    return displayed;
}
