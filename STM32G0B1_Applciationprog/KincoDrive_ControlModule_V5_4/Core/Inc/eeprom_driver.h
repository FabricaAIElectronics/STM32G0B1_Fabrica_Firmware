/**
 * @file    eeprom_driver.h
 * @brief   I2C EEPROM driver — raw access + Config_t load/save.
 *
 * @details This module is a pure data layer.  It never touches GPIOs or
 *          PWMs.  AppLogic is the only consumer and decides when to load
 *          or save.
 *
 *          On-chip layout (page 0, offset 0):
 *            Byte 0     magic  (0xA5 = valid)
 *            Bytes 1–6  Config_t payload (hs_state + 5 fan setpoints)
 *            Byte 7     checksum (XOR of bytes 0–6)
 *
 * @author  jordan
 * @date    2026-04-21
 */

#ifndef EEPROM_DRIVER_H
#define EEPROM_DRIVER_H

#include "stm32g0xx_hal.h"
#include "AppLogic.h"       /* Config_t */
#include <stdbool.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Low-level EEPROM access
 * ════════════════════════════════════════════════════════════════════════════ */

/** Write raw bytes to EEPROM (handles page boundaries). */
void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Read raw bytes from EEPROM. */
void EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Erase a single EEPROM page (fill with 0xFF). */
void EEPROM_pageErase(uint16_t page);

/* ════════════════════════════════════════════════════════════════════════════
 *  Config_t load / save  (called from AppLogic only)
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Read the persisted Config_t out of EEPROM.
 * @param  out  Destination buffer — untouched on failure.
 * @retval true  magic + checksum valid, *out updated.
 * @retval false invalid data in EEPROM; caller should use defaults.
 */
bool EEPROM_Load(Config_t *out);

/**
 * @brief  Persist a Config_t to EEPROM (magic + checksum wrapped).
 */
void EEPROM_Save(const Config_t *in);

#endif /* EEPROM_DRIVER_H */
