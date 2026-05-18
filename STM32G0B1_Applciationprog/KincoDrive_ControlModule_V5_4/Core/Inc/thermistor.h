/**
 * @file    thermistor.h
 * @brief   PTC thermistor temperature reads (status only, no protection trip).
 *
 * @details Reads PTC1..PTC6 from the ADC DMA buffer and converts to °C
 *          using the Steinhart-Hart B-parameter equation. No automatic
 *          shutdown is performed on overtemp — caller decides what to do
 *          with the reading.
 *
 *          Circuit topology:
 *            12V ── NTC ── Node_A
 *                          │── R80 (100 kΩ) ── GND
 *                          └── R79 (180 kΩ) ── Node_B
 *                                              │── R82 (62 kΩ) ── GND
 *                                              └── R81 (2.2 kΩ) ── ADC
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef THERMISTOR_H
#define THERMISTOR_H

#include <stdint.h>
#include "adc_driver.h"

#define THERMISTOR_SUCCESS         0
#define THERMISTOR_ERR_INVALID    (-1)
#define THERMISTOR_ERR_OPEN       (-2)   /* below noise floor → sensor disconnected */
#define THERMISTOR_ERR_RANGE      (-3)   /* result outside -40..+200 °C */

/**
 * @brief  Read a thermistor and convert to °C.
 * @param  channel        ADC channel TEMP_PTC_1..TEMP_PTC_6.
 * @param  temperature_C  Output: integer °C.
 * @retval THERMISTOR_SUCCESS on success, negative error code otherwise.
 */
int Thermistor_Read_C(ADC_Channel_t channel, int32_t *temperature_C);

#endif /* THERMISTOR_H */
