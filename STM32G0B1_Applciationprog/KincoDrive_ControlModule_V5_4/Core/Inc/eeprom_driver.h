/**
 * @file    eeprom_driver.h
 * @brief   I2C EEPROM driver and configuration storage.
 *
 * @details Provides page-based read/write for an I2C EEPROM (64-byte pages,
 *          512 pages), a Config struct with magic validation, EEPROM-backed
 *          config persistence, and CAN command handlers for reading/writing
 *          individual settings over the bus.
 *
 * @author  jordan
 * @date    2025-12-01
 */

#ifndef EEPROM_DRIVER_H
#define EEPROM_DRIVER_H

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Configuration structure (stored in EEPROM page 0)
 * ════════════════════════════════════════════════════════════════════════════ */

typedef struct __attribute__((packed)) {
    uint16_t magic;              /* 0x3584 = valid config */
    uint8_t  mode;
    uint16_t voltage;
    uint16_t count;
    uint16_t pwm0;
    uint16_t pwm1;
    uint16_t hard_over_voltage;  /* mV */
    uint16_t soft_over_voltage;  /* mV */
    uint16_t under_voltage;      /* mV */
    uint16_t over_current;       /* mA */
    uint16_t fan_max_rpm;        /* max fan RPM for % calculation */
} Config;

/* ════════════════════════════════════════════════════════════════════════════
 *  EEPROM setting selectors (for CAN write command)
 * ════════════════════════════════════════════════════════════════════════════ */

#define EEPROM_SETTING_HARD_OV      0x00
#define EEPROM_SETTING_SOFT_OV      0x01
#define EEPROM_SETTING_UNDER_V      0x02
#define EEPROM_SETTING_OVER_CURR    0x03
#define EEPROM_SETTING_FAN_MAX_RPM  0x04

/* ════════════════════════════════════════════════════════════════════════════
 *  EEPROM format status (for async erase operations)
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    EEPROM_IDLE         = 0,
    EEPROM_ERASING      = 1,
    EEPROM_ERASING_DONE = 2
} EEPROM_format_status;

/* ════════════════════════════════════════════════════════════════════════════
 *  Low-level EEPROM access
 * ════════════════════════════════════════════════════════════════════════════ */

/** Write raw bytes to EEPROM (handles page boundaries automatically). */
void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Read raw bytes from EEPROM. */
void EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Write a float value to EEPROM (4 bytes, little-endian). */
void EEPROM_Write_Num(uint16_t page, uint16_t offset, float data);

/** Read a float value from EEPROM. */
float EEPROM_Read_Num(uint16_t page, uint16_t offset);

/** Erase a single EEPROM page (fill with 0xFF). */
void EEPROM_pageErase(uint16_t page);

/* ════════════════════════════════════════════════════════════════════════════
 *  Config management
 * ════════════════════════════════════════════════════════════════════════════ */

/** Populate a Config struct with factory defaults. */
void LoadDefault(Config *config);

/** Check if a Config has a valid magic number. */
bool checkcfg(Config *cfg);

/** Write a Config struct to EEPROM and update the RAM cache. */
void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg);

/** Read a Config struct from EEPROM. */
void EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg);

/** Initialize EEPROM: read config, load defaults if invalid, sanitize. */
void EEPROM_Init(void);

/** Get pointer to cached config (NULL if not yet initialized). */
const Config *EEPROM_GetCachedConfig(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN command handlers
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  CAN handler: write a single config setting.
 *         Payload: [setting_selector, value_lo, value_hi]
 */
void CAN_Handler_EEPROM_Write_Config(uint32_t id, uint8_t *params, uint8_t len);

/**
 * @brief  CAN handler: read and publish all config settings.
 *         Responds with two frames on CAN_ID_EEPROM_ACK and CAN_ID_EEPROM_ACK2.
 */
void CAN_Handler_EEPROM_Read_Config(uint32_t id, uint8_t *params, uint8_t len);

/* Legacy alias (unused but kept for compatibility) */
void CAN_Handler_EEPROM_Write_Overvoltage_Config(uint32_t id, uint8_t *params, uint8_t len);

/* Utility functions */
uint32_t float2Bytes(float float_data);
float bytes2Float(uint8_t buffer[4]);

#endif /* EEPROM_DRIVER_H */
