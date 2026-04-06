/**
 * @file    Power_Electronic.h
 * @brief   High-side power control, ADC sensing, and CAN telemetry packers.
 *
 * @author  jordan
 * @date    2026-03-25
 */

#ifndef POWER_ELECTRONIC_H
#define POWER_ELECTRONIC_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  Return codes
 * ═══════════════════════════════════════════════════════════════════════ */

#define PE_SUCCESS              0
#define PE_ERR_TIMEOUT         (-1)
#define PE_ERR_INVALID_PARAM   (-2)
#define PE_ERR_GEN             (-3)

/* ═══════════════════════════════════════════════════════════════════════
 *  High-side module identifiers
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum {
    HS_MODULE_DRIVE     = 1,
    HS_MODULE_EXTRUDER  = 2,
    HS_MODULE_SCRUBBING = 3
} HighSide_Module_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  ADC channel mapping
 *
 *  Order matches the ADC scan sequence / DMA buffer layout.
 *  Do NOT reorder without updating the CubeMX .ioc file.
 * ═══════════════════════════════════════════════════════════════════════ */

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
} ADC_Peripheral_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  High-side power module control
 * ═══════════════════════════════════════════════════════════════════════ */

int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);
int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);

/* ═══════════════════════════════════════════════════════════════════════
 *  ADC calibration and DMA
 * ═══════════════════════════════════════════════════════════════════════ */

int Calibrate_ADC1(void);
int Start_ADC1_DMA(void);

/* ═══════════════════════════════════════════════════════════════════════
 *  Analog readings
 * ═══════════════════════════════════════════════════════════════════════ */

int  Read_HighSide_Module_Current_mA(HighSide_Module_t module, uint32_t *current_mA);
void Read_24V_Bus_Current_mA(uint32_t *current_mA);
void Read_24V_Voltage_mV(uint32_t *voltage_mV);
void Read_12V_Voltage_mV(uint32_t *voltage_mV);
int  Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, int32_t *temperature_C);

/* ═══════════════════════════════════════════════════════════════════════
 *  12 V buck converter
 * ═══════════════════════════════════════════════════════════════════════ */

int  Enable_12V_Buck_Converter(void);
void Disable_12V_Buck_Converter(void);

/* ═══════════════════════════════════════════════════════════════════════
 *  CAN telemetry data packers
 * ═══════════════════════════════════════════════════════════════════════ */

size_t CAN_Packer_24V_Bus_mV_2Byte(uint8_t *out, size_t out_size);
size_t CAN_Packer_12V_Bus_mV_2Byte(uint8_t *out, size_t out_size);
size_t CAN_Packer_HighSide_Module_Current_mA_2Byte(HighSide_Module_t module, uint8_t *out, size_t out_size);
size_t CAN_Packer_24V_Bus_Current_mA_2Byte(uint8_t *out, size_t out_size);
size_t CAN_Packer_Thermistor_Temp_1Byte(ADC_Peripheral_t thermistor, uint8_t *out, size_t out_size);

#endif /* POWER_ELECTRONIC_H */
