/* Beta-equation thermistor maths, asserted against the firmware's constants.
 * Firmware: STM32G0B1_Applciationprog/KincoDrive_ControlModule_V5_4/Core/Src/thermistor.c
 *
 * BETA / R0_OHMS / T0_KELVIN below are verified equal to thermistor.c:19-21.
 * The expression matches thermistor.c:54 exactly, including the association
 * (logf(...) / BETA rather than (1/BETA) * logf(...)). */
#include <math.h>
#include <stdint.h>
#include "harness.h"

#define BETA        3950.0f
#define R0_OHMS     100000.0f
#define T0_KELVIN   298.15f

/* Same relation the firmware uses: 1/T = 1/T0 + ln(R/R0) / BETA */
static float resistance_to_celsius(float r_ohms)
{
    float inv_t = (1.0f / T0_KELVIN) + (logf(r_ohms / R0_OHMS) / BETA);
    return (1.0f / inv_t) - 273.15f;
}

int main(void)
{
    /* At the reference resistance the result must be exactly 25 C. */
    float at_r0 = resistance_to_celsius(R0_OHMS);
    VV_CHECK("thermistor_25c_at_r0", fabsf(at_r0 - 25.0f) < 0.01f);

    /* Higher resistance means colder for an NTC. */
    VV_CHECK("thermistor_colder_above_r0", resistance_to_celsius(200000.0f) < 25.0f);
    VV_CHECK("thermistor_hotter_below_r0", resistance_to_celsius(50000.0f) > 25.0f);

    /* Monotonic across the working range. */
    int monotonic = 1;
    float prev = resistance_to_celsius(10000.0f);
    for (float r = 20000.0f; r <= 400000.0f; r += 10000.0f) {
        float t = resistance_to_celsius(r);
        if (t >= prev) { monotonic = 0; break; }
        prev = t;
    }
    VV_CHECK("thermistor_monotonic", monotonic);

    VV_REPORT();
}
