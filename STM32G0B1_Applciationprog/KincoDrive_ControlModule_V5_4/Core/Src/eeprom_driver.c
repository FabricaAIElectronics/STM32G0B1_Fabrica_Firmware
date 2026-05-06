/**
 * @file    eeprom_driver.c
 * @brief   I2C EEPROM driver — raw access + Config_t load/save.
 *
 * @author  jordan
 * @date    2026-04-21
 */

#include "eeprom_driver.h"
#include "main.h"

#include <string.h>
#include <stdbool.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  EEPROM hardware constants
 * ════════════════════════════════════════════════════════════════════════════ */

#define EEPROM_I2C_ADDR     0xA0U
#define PAGE_SIZE           64U     /* bytes per page */
#define PAGE_COUNT          512U    /* total pages    */

#define CFG_PAGE            0U
#define CFG_OFFSET          0U
#define CFG_MAGIC           0xA5U

/* On-wire blob: 1B magic + Config_t + 1B checksum. */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    Config_t cfg;
    uint8_t  checksum;
} EEPROM_Blob_t;

extern I2C_HandleTypeDef hi2c1;

/* ════════════════════════════════════════════════════════════════════════════
 *  Low-level EEPROM read / write (page-boundary aware)
 * ════════════════════════════════════════════════════════════════════════════ */

static uint16_t bytes_to_page_end(uint16_t size, uint16_t offset)
{
    uint16_t remaining = (uint16_t)PAGE_SIZE - offset;
    return (size < remaining) ? size : remaining;
}

void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size)
{
    const int page_addr_bits = 6;   /* log2(64) */
    uint16_t current_page = page;
    uint16_t pos = 0;

    while (size > 0) {
        uint16_t mem_addr   = (uint16_t)((current_page << page_addr_bits) | offset);
        uint16_t chunk_size = bytes_to_page_end(size, offset);

        if (HAL_I2C_Mem_Write(&hi2c1, EEPROM_I2C_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT,
                              &data[pos], chunk_size, 1000U) != HAL_OK) {
            return;
        }

        /* Wait for EEPROM internal write cycle */
        for (int retry = 0; retry < 10; ++retry) {
            if (HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDR, 1U, 100U) == HAL_OK) {
                break;
            }
        }

        current_page++;
        offset  = 0U;
        size   -= chunk_size;
        pos    += chunk_size;
    }
}

void EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size)
{
    const int page_addr_bits = 6;
    uint16_t current_page = page;
    uint16_t pos = 0;

    while (size > 0) {
        uint16_t mem_addr   = (uint16_t)((current_page << page_addr_bits) | offset);
        uint16_t chunk_size = bytes_to_page_end(size, offset);

        HAL_I2C_Mem_Read(&hi2c1, EEPROM_I2C_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT,
                         &data[pos], chunk_size, 1000U);

        current_page++;
        offset  = 0U;
        size   -= chunk_size;
        pos    += chunk_size;
    }
}

void EEPROM_pageErase(uint16_t page)
{
    const int page_addr_bits = 6;
    uint16_t mem_addr = (uint16_t)(page << page_addr_bits);

    uint8_t blank[PAGE_SIZE];
    memset(blank, 0xFF, PAGE_SIZE);

    HAL_I2C_Mem_Write(&hi2c1, EEPROM_I2C_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT,
                      blank, PAGE_SIZE, 1000U);
    HAL_Delay(5U);

    for (int retry = 0; retry < 10; ++retry) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDR, 1U, 100U) == HAL_OK) {
            break;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Checksum helper  (XOR of magic + Config_t bytes)
 * ════════════════════════════════════════════════════════════════════════════ */

static uint8_t compute_checksum(const EEPROM_Blob_t *b)
{
    uint8_t cs = b->magic;
    const uint8_t *p = (const uint8_t *)&b->cfg;
    for (uint8_t i = 0; i < (uint8_t)sizeof(Config_t); ++i) {
        cs ^= p[i];
    }
    return cs;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Config_t load / save
 * ════════════════════════════════════════════════════════════════════════════ */

bool EEPROM_Load(Config_t *out)
{
    if (out == NULL) return false;

    EEPROM_Blob_t blob;
    EEPROM_Read(CFG_PAGE, CFG_OFFSET, (uint8_t *)&blob, (uint16_t)sizeof(blob));

    if (blob.magic != CFG_MAGIC)              return false;
    if (compute_checksum(&blob) != blob.checksum) return false;

    /* Clamp fan values to 0–100 in case of bit rot. */
    if (blob.cfg.fan_dr > 100U) blob.cfg.fan_dr = 0U;
    if (blob.cfg.fan_ep > 100U) blob.cfg.fan_ep = 0U;
    if (blob.cfg.fan_eh > 100U) blob.cfg.fan_eh = 0U;
    if (blob.cfg.fan_st > 100U) blob.cfg.fan_st = 0U;
    if (blob.cfg.fan_sf > 100U) blob.cfg.fan_sf = 0U;

    *out = blob.cfg;
    return true;
}

void EEPROM_Save(const Config_t *in)
{
    if (in == NULL) return;

    EEPROM_Blob_t blob;
    blob.magic    = CFG_MAGIC;
    blob.cfg      = *in;
    blob.checksum = compute_checksum(&blob);

    EEPROM_Write(CFG_PAGE, CFG_OFFSET, (uint8_t *)&blob, (uint16_t)sizeof(blob));
}
