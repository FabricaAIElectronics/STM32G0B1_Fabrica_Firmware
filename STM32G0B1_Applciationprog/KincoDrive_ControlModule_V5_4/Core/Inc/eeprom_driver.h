/**
 * @file    eeprom_driver.h
 * @brief   I2C EEPROM driver and startup configuration storage.
 *
 * @details Page-based read/write (64-byte pages, 512 pages) plus a
 *          structured startup-config record stored at page 0, offset 0.
 *
 *          Config layout (24 bytes, packed):
 *            Byte 0     magic              0xA6 = current layout
 *            Byte 1     hs_state           bit0=DR, bit1=E, bit2=SC, bit3=VBUCK
 *            Byte 2..6  fan_dr/ep/eh/st/sf 0–100 %
 *            Byte 7..8  oc_dr_mA           uint16 LE
 *            Byte 9..10 oc_e_mA            uint16 LE
 *            Byte 11..12 oc_sc_mA          uint16 LE
 *            Byte 13..14 uv_24V_mV         uint16 LE
 *            Byte 15..16 uv_12V_mV         uint16 LE
 *            Byte 17..22 reserved          (zero)
 *            Byte 23    checksum           XOR of bytes 0..22
 *
 *          Magic byte was bumped from 0xA5 → 0xA6 with the layout change so
 *          existing devices fall back to safe defaults on first boot.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef EEPROM_DRIVER_H
#define EEPROM_DRIVER_H

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define EEPROM_MAGIC    0xA5U

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  hs_state;          /* bit0=DR, bit1=E, bit2=SC, bit3=VBUCK */
    uint8_t  fan_dr;
    uint8_t  fan_ep;
    uint8_t  fan_eh;
    uint8_t  fan_st;
    uint8_t  fan_sf;
    uint16_t oc_dr_mA;
    uint16_t oc_e_mA;
    uint16_t oc_sc_mA;
    uint16_t uv_24V_mV;
    uint16_t uv_12V_mV;
    uint8_t  reserved[6];
    uint8_t  checksum;
} EEPROM_StartupConfig_t;

/* hs_state bitmask helpers */
#define EEPROM_HS_DR    (1U << 0)
#define EEPROM_HS_E     (1U << 1)
#define EEPROM_HS_SC    (1U << 2)
#define EEPROM_HS_VBUCK (1U << 3)

/* ────────── Low-level EEPROM access ────────── */

void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);
void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);
void EEPROM_pageErase(uint16_t page);

/* ────────── Startup config management ────────── */

/**
 * @brief  Read config from EEPROM into RAM cache. If magic or checksum is
 *         invalid, load hard-coded safe defaults into the cache (does NOT
 *         write to EEPROM).
 */
void EEPROM_Init(void);

/** Reset the RAM cache to safe defaults (HS off, fans 0, OC=5A, UV=20V/10V). */
void EEPROM_LoadAndApplyDefaults(void);

/**
 * @brief  Apply the cached config to hardware:
 *           - HS channels set per cfg->hs_state
 *           - Fan PWMs set per cfg->fan_xx
 *           - OC + UV thresholds pushed to power_monitor
 */
void EEPROM_ApplyStartupConfig(void);

/** Snapshot current HS GPIOs, fan setpoints, and OC/UV thresholds → EEPROM. */
void EEPROM_SaveStartupConfig(void);

/** Pointer to the cached config (never NULL). */
const EEPROM_StartupConfig_t *EEPROM_GetCachedConfig(void);

#endif /* EEPROM_DRIVER_H */
