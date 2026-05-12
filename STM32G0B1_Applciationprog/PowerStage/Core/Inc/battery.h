/**
 * @file    battery.h
 * @brief   6S Lithium battery state-of-charge estimator with IR compensation.
 *
 * @details Pack: 6S Li-ion / Li-Po (4.20 V/cell fully charged → 25.20 V pack;
 *          3.27 V/cell cutoff → 19.60 V pack). The estimator uses an OCV→SOC
 *          lookup table with linear interpolation, plus a simple internal-
 *          resistance correction so that the reported SOC doesn't sag when
 *          load current is high.
 *
 *          V_OCV = V_meas + I_total × R_int
 *          SOC   = lookup(V_OCV) → 0..100 %
 *
 *          Tune `BATTERY_INT_R_MILLIOHM` for your specific pack — 200 mΩ
 *          is a reasonable midpoint for 6S 18650 packs (≈30 mΩ × 6 cells
 *          + ≈20 mΩ wiring + connectors).
 *
 * @author  jordan
 * @date    2026-05-07
 */

#ifndef INC_BATTERY_H_
#define INC_BATTERY_H_

#include <stdint.h>

/* ====================== Configuration ====================== */

/** Cutoff voltage for 6S pack (3.27 V/cell × 6). Below this the SOC saturates
 *  at 0 %. Used by the host as a "shut down everything" reference. */
#define BATTERY_CUTOFF_MV       19600U

/** Fully-charged reference voltage for 6S pack (4.20 V/cell × 6). */
#define BATTERY_FULL_MV         25200U

/** Estimated total internal resistance of the pack in milliohms.
 *  Used to undo the load-induced voltage sag before the OCV→SOC lookup. */
#define BATTERY_INT_R_MILLIOHM  200U

/* ====================== SOC filter tuning ====================== */

/** EMA shift for the raw-SOC low-pass filter. With α = 1/(1<<SHIFT) and
 *  100 ms sample period, time-constant ≈ (1<<SHIFT)/10 seconds.
 *  4 → 1.6 s     5 → 3.2 s     6 → 6.4 s. */
#define BATTERY_SOC_EMA_SHIFT      4U

/** Asymmetric hysteresis (in % SOC) before the displayed value tracks the
 *  smoothed estimate.
 *
 *    DROP: how far the smoothed SOC must sit below the displayed value
 *          before we accept a step-down. Small → SOC follows discharge
 *          quickly; large → very stable but slow to register depletion.
 *
 *    RISE: same for upward steps. Set deliberately HIGHER than DROP so a
 *          brief load drop / rebound doesn't bounce SOC upward — only
 *          sustained recovery (i.e. real charging) gets through.
 */
#define BATTERY_SOC_HYST_DROP_PCT  1U
#define BATTERY_SOC_HYST_RISE_PCT  3U

/* ====================== Public API ========================= */

/**
 * @brief  Estimate raw state-of-charge (0..100 %) for a 6S Li-ion/Li-Po pack
 *         from a live V24 reading and battery current draw. Uses OCV lookup
 *         with internal-resistance compensation. Output is noisy — feed it
 *         into Battery_FilterSOC_pct() for a stable display value.
 */
uint8_t Battery_EstimateSOC_pct(uint16_t v24_mV, uint16_t i_bat_mA);

/**
 * @brief  Smooth the raw SOC into a "displayed" value that only steps when
 *         degradation (or sustained recovery) is confirmed.
 *
 *         Internal state is `static` and self-initialises on first call —
 *         the first sample snaps directly to the raw value, so boot SOC is
 *         immediate. Subsequent samples pass through:
 *
 *           1. EMA low-pass with α = 1/(1<<BATTERY_SOC_EMA_SHIFT)
 *           2. Asymmetric hysteresis (DROP < RISE) before the displayed
 *              value steps by ±1 %.
 *
 *         Net effect: small jitter is invisible; real depletion is tracked
 *         in 1 %/tick steps; voltage rebounds after a load spike don't push
 *         SOC back up unless the new equilibrium really is higher.
 *
 * @param  raw_soc_pct  Output of Battery_EstimateSOC_pct().
 * @retval Displayed SOC (0..100 %).
 */
uint8_t Battery_FilterSOC_pct(uint8_t raw_soc_pct);

/**
 * @brief  Convenience: returns the configured cutoff voltage in mV.
 *         Useful for the host to display a "low battery" warning consistently.
 */
static inline uint16_t Battery_GetCutoffVoltage_mV(void) { return BATTERY_CUTOFF_MV; }

#endif /* INC_BATTERY_H_ */
