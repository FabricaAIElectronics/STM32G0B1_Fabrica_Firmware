/**
 * @file    eeprom_driver.h
 * @brief   I2C EEPROM driver and startup configuration storage.
 *
 * @details Provides page-based read/write for an I2C EEPROM (64-byte pages,
 *          512 pages), an 8-byte startup config struct with magic + XOR
 *          checksum, and functions to save/load/apply the config over I2C.
 *
 *          Config layout (page 0, offset 0):
 *            Byte 0  magic    (0xA5 = valid)
 *            Byte 1  hs_state (bit0=DR, bit1=E, bit2=SC, bit3=VBUCK)
 *            Byte 2  fan_dr   (0–100 %)
 *            Byte 3  fan_ep   (0–100 %)
 *            Byte 4  fan_eh   (0–100 %)
 *            Byte 5  fan_st   (0–100 %)
 *            Byte 6  fan_sf   (0–100 %)
 *            Byte 7  checksum (XOR of bytes 0–6)
 *
 * @author  jordan
 * @date    2026-03-26
 */

#ifndef EEPROM_DRIVER_H
#define EEPROM_DRIVER_H

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Startup configuration structure (stored in EEPROM page 0, offset 0)
 * ════════════════════════════════════════════════════════════════════════════ */

#define EEPROM_MAGIC    0xA5U

typedef struct __attribute__((packed)) {
    uint8_t magic;      /* 0xA5 = valid config                        */
    uint8_t hs_state;   /* bit0=DR, bit1=E, bit2=SC, bit3=VBUCK (1=ON) */
    uint8_t fan_dr;     /* Drive fan default speed (0–100 %)          */
    uint8_t fan_ep;     /* Extruder Platform fan  (0–100 %)           */
    uint8_t fan_eh;     /* Extruder Head fan      (0–100 %)           */
    uint8_t fan_st;     /* Stepper fan            (0–100 %)           */
    uint8_t fan_sf;     /* Scrubbing Front fan    (0–100 %)           */
    uint8_t checksum;   /* XOR of bytes 0–6                           */
} EEPROM_StartupConfig_t;

/* hs_state bitmask helpers */
#define EEPROM_HS_DR    (1U << 0)
#define EEPROM_HS_E     (1U << 1)
#define EEPROM_HS_SC    (1U << 2)
#define EEPROM_HS_VBUCK (1U << 3)

/* ════════════════════════════════════════════════════════════════════════════
 *  Low-level EEPROM access
 * ════════════════════════════════════════════════════════════════════════════ */

/** Write raw bytes to EEPROM (handles page boundaries automatically). */
void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Read raw bytes from EEPROM. */
void EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);

/** Erase a single EEPROM page (fill with 0xFF). */
void EEPROM_pageErase(uint16_t page);

/* ════════════════════════════════════════════════════════════════════════════
 *  Startup config management
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Initialize EEPROM: read config into RAM cache.
 *         If magic or checksum is invalid, loads hardcoded safe defaults
 *         (all HS OFF, all fans 0 %) into the cache but does NOT write to
 *         EEPROM — call EEPROM_SaveStartupConfig() explicitly if desired.
 */
void EEPROM_Init(void);

/**
 * @brief  Apply the cached startup config to hardware.
 *         Calls Enable/Disable for each HS channel and set_Fan_PWM() for
 *         each fan.  Safe to call before peripheral clocks are ready only
 *         if hardware is already init'd.
 */
void EEPROM_ApplyStartupConfig(void);

/**
 * @brief  Snapshot current HS GPIO states and fan PWM setpoints, write to
 *         EEPROM, and update the RAM cache.
 */
void EEPROM_SaveStartupConfig(void);

/**
 * @brief  Return pointer to the cached startup config (never NULL — returns
 *         hardcoded defaults if EEPROM was invalid at init).
 */
const EEPROM_StartupConfig_t *EEPROM_GetCachedConfig(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  Utility
 * ════════════════════════════════════════════════════════════════════════════ */

uint32_t float2Bytes(float float_data);
float    bytes2Float(uint8_t buffer[4]);

#endif /* EEPROM_DRIVER_H */
