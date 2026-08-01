/* Tests the REAL PowerStage battery module, compiled for the host.
 *
 * Firmware: STM32G0B1_Applciationprog/PowerStage/Core/Src/battery.c
 *
 * battery.c includes only battery.h, which includes only <stdint.h> and
 * <stdbool.h> — no HAL, no CMSIS. So the actual module is compiled and linked
 * into this binary rather than having its maths restated here. That makes these
 * assertions an independent check of the shipped code, not a copy of it.
 *
 * The plan originally modelled SOC as linear interpolation between cutoff and
 * full. The firmware does not do that: it uses a 12-point OCV lookup table with
 * internal-resistance compensation, and the two disagree by up to 25 points.
 * These tests assert what the firmware actually does.
 */
#include <stdint.h>
#include "harness.h"
#include "battery.h"

int main(void)
{
    /* ---- Curve endpoints, no load (I = 0 so no IR compensation) ---- */
    VV_EQ_U32("soc_at_full",        Battery_EstimateSOC_pct(25200, 0), 100u);
    VV_EQ_U32("soc_above_full",     Battery_EstimateSOC_pct(30000, 0), 100u);
    VV_EQ_U32("soc_at_cutoff",      Battery_EstimateSOC_pct(19600, 0), 0u);
    VV_EQ_U32("soc_below_cutoff",   Battery_EstimateSOC_pct(18000, 0), 0u);

    /* ---- Exact curve points must return their tabulated SOC ---- */
    VV_EQ_U32("soc_curve_24600_90", Battery_EstimateSOC_pct(24600, 0), 90u);
    VV_EQ_U32("soc_curve_23100_50", Battery_EstimateSOC_pct(23100, 0), 50u);
    VV_EQ_U32("soc_curve_22200_20", Battery_EstimateSOC_pct(22200, 0), 20u);
    VV_EQ_U32("soc_curve_21600_10", Battery_EstimateSOC_pct(21600, 0), 10u);
    VV_EQ_U32("soc_curve_20400_5",  Battery_EstimateSOC_pct(20400, 0), 5u);

    /* ---- Interpolation between two points ----
     * 23250 mV sits halfway between {23100,50} and {23400,60} => 55. */
    VV_EQ_U32("soc_interpolated_midpoint",
              Battery_EstimateSOC_pct(23250, 0), 55u);

    /* ---- IR compensation raises the effective OCV ----
     * R_int = 200 mOhm, so 5000 mA adds 5000*200/1000 = 1000 mV.
     * 22100 mV under 5 A load behaves as 23100 mV open-circuit => 50 %. */
    VV_EQ_U32("soc_ir_compensation_5A",
              Battery_EstimateSOC_pct(22100, 5000), 50u);
    VV_CHECK("soc_load_reads_higher_than_no_load",
             Battery_EstimateSOC_pct(22100, 5000) >
             Battery_EstimateSOC_pct(22100, 0));

    /* ---- Monotonic non-decreasing across the whole pack range ---- */
    int monotonic = 1;
    uint8_t prev = 0;
    for (uint32_t mv = 19000; mv <= 26000; mv += 25) {
        uint8_t s = Battery_EstimateSOC_pct((uint16_t)mv, 0);
        if (s < prev) { monotonic = 0; break; }
        prev = s;
    }
    VV_CHECK("soc_monotonic_in_voltage", monotonic);

    /* ---- Result is always a valid percentage ---- */
    int in_range = 1;
    for (uint32_t mv = 15000; mv <= 30000; mv += 37) {
        uint8_t s = Battery_EstimateSOC_pct((uint16_t)mv, 0);
        if (s > 100u) { in_range = 0; break; }
    }
    VV_CHECK("soc_never_exceeds_100", in_range);

    /* ---- Low-SOC threshold accessor round-trips ---- */
    Battery_SetLowSocThreshold_pct(25);
    VV_EQ_U32("soc_threshold_roundtrip", Battery_GetLowSocThreshold_pct(), 25u);
    VV_CHECK("soc_is_low_below_threshold",  Battery_IsLow(20));
    VV_CHECK("soc_not_low_above_threshold", !Battery_IsLow(30));

    /* ---- Documented pack constants ---- */
    VV_EQ_U32("battery_cutoff_mv", BATTERY_CUTOFF_MV, 19600u);
    VV_EQ_U32("battery_full_mv",   BATTERY_FULL_MV,   25200u);
    VV_EQ_U32("battery_int_r",     BATTERY_INT_R_MILLIOHM, 200u);

    VV_REPORT();
}
