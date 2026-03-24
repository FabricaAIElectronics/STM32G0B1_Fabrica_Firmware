/**
 * @file    Power_Electronic.h
 * @brief   High-side power module control, ADC sensing, and CAN data packers.
 *
 * @details Manages three high-side power switches (Drive, Extruder, Scrubbing),
 *          a 12V buck converter, ADC-based current/voltage/temperature sensing,
 *          overcurrent/overvoltage shutdown protection, and CAN payload packing
 *          for telemetry broadcast.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef POWER_ELECTRONIC_H
#define POWER_ELECTRONIC_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"

/* ════════════════════════════════════════════════════════════════════════════
 *  Return codes
 * ════════════════════════════════════════════════════════════════════════════ */

#define PE_SUCCESS              0
#define PE_ERR_TIMEOUT         (-1)
#define PE_ERR_INVALID_PARAM   (-2)
#define PE_ERR_GEN             (-3)

/* ════════════════════════════════════════════════════════════════════════════
 *  High-side module identifiers
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    HS_MODULE_DRIVE     = 1,
    HS_MODULE_EXTRUDER  = 2,
    HS_MODULE_SCRUBBING = 3
} HighSide_Module_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC channel mapping
 *
 *  IMPORTANT: The enum order matches the ADC scan sequence and DMA buffer
 *  layout.  Do NOT reorder without updating the STM32CubeMX .ioc file.
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
    VADC_24         = 9,    /* 24V bus voltage divider */
    VADC_12         = 10,   /* 12V bus voltage divider */
    CURR_MON_24V    = 11,   /* 24V bus current sense */
    ADC_BUF_LEN     = 12    /* total DMA buffer length */
} ADC_Peripheral_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  High-side power module control
 * ════════════════════════════════════════════════════════════════════════════ */

/** Enable a high-side module, wait for power-good up to timeout_ms. */
int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);

/** Disable a high-side module immediately. */
int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);

/** Enable all three high-side modules sequentially. */
int Enable_HighSide_Power_All(uint32_t timeout_ms);

/** Disable all three high-side modules. */
int Disable_HighSide_Power_All(uint32_t timeout_ms);

/* ════════════════════════════════════════════════════════════════════════════
 *  ADC calibration and DMA
 * ════════════════════════════════════════════════════════════════════════════ */

/** Run ADC1 self-calibration.  Call before Start_ADC1_DMA(). */
int Calibrate_ADC1(void);

/** Start continuous ADC1 → DMA conversion into the internal buffer. */
int Start_ADC1_DMA(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  Analog readings
 * ════════════════════════════════════════════════════════════════════════════ */

/** Read per-module current (milliamperes) from the IMON sense pin. */
int Read_HighSide_Module_Current_mA(HighSide_Module_t module, uint32_t *current_mA);

/** Read 24V bus current (milliamperes). */
void Read_24V_Bus_Current_mA(uint32_t *current_mA);

/** Read 24V bus voltage in 0.1V units (e.g. 240 = 24.0V). */
void Read_24V_Voltage_1DP(uint32_t *voltage_1DP);

/** Read 12V bus voltage in 0.1V units. */
void Read_12V_Voltage_1DP(uint32_t *voltage_1DP);

/** Read thermistor temperature in whole degrees Celsius. */
int Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, uint32_t *temperature_C);

/* ════════════════════════════════════════════════════════════════════════════
 *  12V buck converter
 * ════════════════════════════════════════════════════════════════════════════ */

/** Enable 12V buck — only if 24V bus is above minimum threshold. */
int Enable_12V_Buck_Converter(void);

/** Disable 12V buck immediately. */
void Disable_12V_Buck_Converter(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  Overcurrent / Overvoltage protection
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Check for overcurrent/overvoltage conditions and shut down
 *         affected modules.  Call periodically from the main loop.
 */
void Shutdown_Protection(void);

/**
 * @brief  Reset internal edge-detection state of Shutdown_Protection().
 *         Call when transitioning out of an error/recovery state so that
 *         faults can re-trigger on the next check cycle.
 */
void Shutdown_Protection_ResetState(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN command handlers (high-side power on/off)
 * ════════════════════════════════════════════════════════════════════════════ */

/** CAN handler: enable/disable Drive high-side power. */
void CAN_Handler_Set_HS_Drive_Power(uint32_t id, uint8_t *params, uint8_t len);

/** CAN handler: enable/disable Extruder high-side power. */
void CAN_Handler_Set_HS_Extruder_Power(uint32_t id, uint8_t *params, uint8_t len);

/** CAN handler: enable/disable Scrubbing high-side power. */
void CAN_Handler_Set_HS_Scrubbing_Power(uint32_t id, uint8_t *params, uint8_t len);

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN telemetry data packers
 * ════════════════════════════════════════════════════════════════════════════ */

/** Pack 24V bus voltage into 2 CAN bytes (u16 LE, 0.1V). */
size_t CAN_Packer_24V_Bus_2Byte(uint8_t *out, size_t out_size);

/** Pack 12V bus voltage into 1 CAN byte (u8, 0.1V). */
size_t CAN_Packer_12V_Bus_1Byte(uint8_t *out, size_t out_size);

/** Pack per-module current into 1 CAN byte (u8, 0.1A). */
size_t CAN_Packer_HighSide_Module_Current_1DP_1Byte(HighSide_Module_t module, uint8_t *out, size_t out_size);

/** Pack 24V bus current into 2 CAN bytes (u16 LE, 0.1A). */
size_t CAN_Packer_24V_Bus_Current_1DP_2Byte(uint8_t *out, size_t out_size);

/** Pack thermistor temperature into 1 CAN byte (offset: wire = degC + 40). */
size_t CAN_Packer_Thermistor_Temp_1Byte(ADC_Peripheral_t thermistor, uint8_t *out, size_t out_size);

/** Pack overcurrent + overvoltage protection state into 1 CAN byte. */
size_t CAN_Packer_Protection_State_1Byte(uint8_t *out, size_t out_size);

#endif /* POWER_ELECTRONIC_H */
