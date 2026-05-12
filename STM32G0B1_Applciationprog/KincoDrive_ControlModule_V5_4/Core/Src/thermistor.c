/**
 * @file    thermistor.c
 * @brief   PTC thermistor read + Steinhart-Hart B-parameter conversion.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "thermistor.h"
#include <math.h>

#define V_SUPPLY        12.0f
#define R80             100000.0f         /* parallel-to-ground at Node_A    */
#define R79             180000.0f         /* series Node_A → Node_B          */
#define R82             62000.0f          /* parallel-to-ground at Node_B    */
#define VREF            3.3f
#define ADC_MAX_F       4095.0f

#define BETA            3950.0f           /* NTC beta value                  */
#define R0_OHMS         100000.0f         /* resistance at 25 °C             */
#define T0_KELVIN       298.15f           /* 25 °C in K                      */

#define V_NOISE_FLOOR   0.010f            /* below this = sensor open        */
#define TEMP_MIN_C      (-40)
#define TEMP_MAX_C      (200)

int Thermistor_Read_C(ADC_Channel_t channel, int32_t *temperature_C)
{
    if (temperature_C == NULL) return THERMISTOR_ERR_INVALID;
    if (channel < TEMP_PTC_1 || channel > TEMP_PTC_6) return THERMISTOR_ERR_INVALID;

    uint16_t adc = ADC_VAL[channel];
    if (adc == 0U) return THERMISTOR_ERR_OPEN;

    /* Step 1: ADC count → V at the ADC pin (V_VPTC). */
    float v_vptc = ((float)adc / ADC_MAX_F) * VREF;
    if (v_vptc < V_NOISE_FLOOR) return THERMISTOR_ERR_OPEN;

    /* Step 2: undo the R79/R82 attenuator → recover V at Node_A. */
    const float r_chain = R79 + R82;            /* 242 kΩ */
    float v_node_a = v_vptc * r_chain / R82;
    if (v_node_a >= V_SUPPLY) return THERMISTOR_ERR_RANGE;

    /* Step 3: NTC resistance from voltage divider at Node_A.
     *   V_Node_A = V_SUPPLY × R_load / (R_ntc + R_load)
     *   R_load   = R80 || (R79 + R82)
     *   R_ntc    = R_load × (V_SUPPLY / V_Node_A − 1)
     */
    const float r_load = (R80 * r_chain) / (R80 + r_chain);  /* ≈ 70 760 Ω */
    float r_ntc = r_load * ((V_SUPPLY / v_node_a) - 1.0f);
    if (r_ntc <= 0.0f) return THERMISTOR_ERR_RANGE;

    /* Step 4: Beta equation → T (Kelvin) → °C. */
    float inv_T = (1.0f / T0_KELVIN) + (logf(r_ntc / R0_OHMS) / BETA);
    if (inv_T <= 0.0f) return THERMISTOR_ERR_RANGE;

    float t_C = (1.0f / inv_T) - 273.15f;
    if (t_C < (float)TEMP_MIN_C || t_C > (float)TEMP_MAX_C) return THERMISTOR_ERR_RANGE;

    *temperature_C = (int32_t)t_C;
    return THERMISTOR_SUCCESS;
}
