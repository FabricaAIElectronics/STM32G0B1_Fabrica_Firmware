/*
 * eeprom_driver.h
 *
 *  Created on: 1 Dec 2025
 *      Author: jordan
 */

#ifndef INC_EEPROM_H_
#define INC_EEPROM_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>

/* ============================================================
 * Magic number — identifies a valid PowerStage config in EEPROM
 * Change this if Config struct layout changes (forces re-init)
 * ============================================================ */
#define CONFIG_MAGIC            0xAB12

/* ============================================================
 * Rail count — must match PowerRailIndex_t in io_module.h
 * Defined here to avoid circular include with io_module.h
 * ============================================================ */
#define CONFIG_RAIL_COUNT       5       // AUX, LED, DRIVE, CAP, SBC

/* ============================================================
 * Config — persisted to EEPROM
 * __attribute__((packed)) ensures no padding bytes so
 * sizeof(Config) == sum of members (safe for raw read/write)
 *
 * Layout (24 bytes total):
 *   Offset  0 : magic            (uint16_t)
 *   Offset  2 : fan_default_mode (uint8_t)
 *   Offset  3 : fan_default_duty (uint8_t)
 *   Offset  4 : fan_min_duty     (uint8_t)
 *   Offset  5 : fan_auto_on_temp (uint8_t)
 *   Offset  6 : fan_auto_off_temp(uint8_t)
 *   Offset  7 : hs_default_state (uint8_t)  bitmask, bit per rail
 *   Offset  8 : oc_threshold_mA  (uint16_t × 5 = 10 bytes)
 *   Offset 18 : uv_V24_mV        (uint16_t)
 *   Offset 20 : uv_VCAP_mV       (uint16_t)
 *   Offset 22 : uv_V12_mV        (uint16_t)
 *   Total  24 bytes
 * ============================================================ */
typedef struct __attribute__((packed)) {

    uint16_t magic;                                 // CONFIG_MAGIC when valid

    /* ---- Fan defaults (applied on boot) ---- */
    uint8_t  fan_default_mode;                      // 0=OFF, 1=ON_MANUAL, 2=AUTO
    uint8_t  fan_default_duty;                      // default duty cycle  0–100 %
    uint8_t  fan_min_duty;                          // minimum duty before stall 0–100 %
    uint8_t  fan_auto_on_temp;                      // AUTO mode: turn ON  above this °C
    uint8_t  fan_auto_off_temp;                     // AUTO mode: turn OFF below this °C (hysteresis)

    /* ---- HS switch defaults (boot state bitmask) ---- */
    /* bit0=RAIL_AUX, bit1=RAIL_LED, bit2=RAIL_DRIVE, bit3=RAIL_CAP, bit4=RAIL_SBC */
    uint8_t  hs_default_state;

    /* ---- Overcurrent warning thresholds per rail (mA) ---- */
    /* Index matches PowerRailIndex_t: [AUX, LED, DRIVE, CAP, SBC] */
    uint16_t oc_threshold_mA[CONFIG_RAIL_COUNT];

    /* ---- Undervoltage thresholds (mV) ---- */
    uint16_t uv_V24_mV;                            // V24  UV trip point (e.g. 20000 = 20 V)
    uint16_t uv_VCAP_mV;                           // VCAP UV trip point
    uint16_t uv_V12_mV;                            // V12  UV trip point (e.g. 10000 = 10 V)

} Config;

/* ============================================================
 * EEPROM state machine (for non-blocking erase operations)
 * ============================================================ */
typedef enum {
    EEPROM_IDLE         = 0,
    EEPROM_ERASING,
    EEPROM_ERASING_DONE
} EEPROM_format_status;

/* ============================================================
 * Function prototypes
 * ============================================================ */

/* Raw byte read / write */
void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);
void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/* Float helpers */
uint32_t float2Bytes(float float_data);
float    bytes2Float(uint8_t buffer[4]);
void     EEPROM_Write_Num(uint16_t page, uint16_t offset, float data);
float    EEPROM_Read_Num (uint16_t page, uint16_t offset);

/* Page erase */
void EEPROM_pageErase(uint16_t page);

/* Config struct helpers */
void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg);
void EEPROM_Read_Config (uint16_t page, uint16_t offset, Config *cfg);

/* Default config and validation */
void LoadDefault(Config *config);
bool checkcfg   (Config *cfg);

#endif /* INC_EEPROM_H_ */
