/**
 * @file    adc_driver.h
 * @brief   ADC1 calibration, DMA buffer, and channel index map.
 *
 * @details ADC1 scans 12 channels in a fixed sequence; the DMA buffer
 *          ADC_VAL[] is updated continuously. The channel order matches the
 *          CubeMX-generated MX_ADC1_Init() in main.c — do NOT reorder
 *          without updating the .ioc file.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Return codes
 * ════════════════════════════════════════════════════════════════════════════ */

#define ADC_SUCCESS              0
#define ADC_ERR_GEN             (-1)

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC channel mapping (matches DMA buffer layout)
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    TEMP_PTC_1      = 0,
    TEMP_PTC_2      = 1,
    TEMP_PTC_3      = 2,
    TEMP_PTC_4      = 3,
    TEMP_PTC_5      = 4,
    TEMP_PTC_6      = 5,
    CURR_MON_1      = 6,    /* drive module current sense */
    CURR_MON_2      = 7,    /* extruder module current sense */
    CURR_MON_3      = 8,    /* scrubbing module current sense */
    VADC_24         = 9,    /* 24 V bus voltage divider */
    VADC_12         = 10,   /* 12 V bus voltage divider */
    CURR_MON_24V    = 11,   /* 24 V bus current sense */
    ADC_BUF_LEN     = 12
} ADC_Channel_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

/** Continuously-updated DMA buffer. Read directly via the ADC_Channel_t indexes. */
extern volatile uint16_t ADC_VAL[ADC_BUF_LEN];

/** Run ADC self-calibration. Call once at boot before Start_ADC1_DMA(). */
int  Calibrate_ADC1(void);

/** Start continuous DMA into ADC_VAL[]. Re-armed automatically by HAL_ADC_ConvCpltCallback(). */
int  Start_ADC1_DMA(void);

/** Convert raw ADC count (0..4095) to millivolts at the ADC pin (VREF = 3300 mV). */
uint32_t adc_to_mV(uint16_t adc);

#endif /* ADC_DRIVER_H */
