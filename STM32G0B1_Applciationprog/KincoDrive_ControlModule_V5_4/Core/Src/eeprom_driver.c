/**
 * @file    eeprom_driver.c
 * @brief   I2C EEPROM driver and startup configuration storage.
 *
 * @author  jordan
 * @date    2026-03-26
 */

#include "eeprom_driver.h"
#include "Power_Electronic.h"
#include "Fan_PWM.h"
#include "main.h"

#include <math.h>
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

extern I2C_HandleTypeDef hi2c1;

/* ════════════════════════════════════════════════════════════════════════════
 *  RAM cache
 * ════════════════════════════════════════════════════════════════════════════ */

static EEPROM_StartupConfig_t cached_cfg;

/* ════════════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ════════════════════════════════════════════════════════════════════════════ */

static uint8_t compute_checksum(const EEPROM_StartupConfig_t *cfg)
{
    uint8_t cs = 0;
    const uint8_t *p = (const uint8_t *)cfg;
    for (uint8_t i = 0; i < (uint8_t)(sizeof(EEPROM_StartupConfig_t) - 1U); ++i) {
        cs ^= p[i];
    }
    return cs;
}

static bool config_is_valid(const EEPROM_StartupConfig_t *cfg)
{
    if (cfg->magic != EEPROM_MAGIC) return false;
    return (compute_checksum(cfg) == cfg->checksum);
}

static void load_safe_defaults(EEPROM_StartupConfig_t *cfg)
{
    cfg->magic    = EEPROM_MAGIC;
    cfg->hs_state = 0x00U;   /* all HS OFF */
    cfg->fan_dr   = 0U;
    cfg->fan_ep   = 0U;
    cfg->fan_eh   = 0U;
    cfg->fan_st   = 0U;
    cfg->fan_sf   = 0U;
    cfg->checksum = compute_checksum(cfg);
}

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
 *  Float utility (kept for compatibility with any callers)
 * ════════════════════════════════════════════════════════════════════════════ */

uint32_t float2Bytes(float float_data)
{
    uint32_t raw;
    memcpy(&raw, &float_data, sizeof(raw));
    return raw;
}

float bytes2Float(uint8_t buffer[4])
{
    float f;
    uint32_t raw = ((uint32_t)buffer[3] << 24) | ((uint32_t)buffer[2] << 16) |
                   ((uint32_t)buffer[1] <<  8) |  (uint32_t)buffer[0];
    memcpy(&f, &raw, sizeof(f));
    return f;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Startup config public API
 * ════════════════════════════════════════════════════════════════════════════ */

void EEPROM_Init(void)
{
    EEPROM_StartupConfig_t cfg;
    EEPROM_Read(CFG_PAGE, CFG_OFFSET, (uint8_t *)&cfg, (uint16_t)sizeof(cfg));

    if (config_is_valid(&cfg)) {
        /* Clamp fan values to 0–100 just in case of bit rot */
        if (cfg.fan_dr > 100U) cfg.fan_dr = 0U;
        if (cfg.fan_ep > 100U) cfg.fan_ep = 0U;
        if (cfg.fan_eh > 100U) cfg.fan_eh = 0U;
        if (cfg.fan_st > 100U) cfg.fan_st = 0U;
        if (cfg.fan_sf > 100U) cfg.fan_sf = 0U;
        cached_cfg = cfg;
    } else {
        /* Invalid EEPROM — use safe defaults in RAM; don't write to EEPROM */
        load_safe_defaults(&cached_cfg);
    }
}

void EEPROM_LoadAndApplyDefaults(void)
{
    load_safe_defaults(&cached_cfg);
    EEPROM_ApplyStartupConfig();
}

void EEPROM_ApplyStartupConfig(void)
{
    const EEPROM_StartupConfig_t *cfg = &cached_cfg;

    /* ── High-side switches ── */
    if (cfg->hs_state & EEPROM_HS_DR) {
        Enable_HighSide_Power_Module(HS_MODULE_DRIVE, 0U);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_DRIVE, 0U);
    }

    if (cfg->hs_state & EEPROM_HS_E) {
        Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 0U);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 0U);
    }

    if (cfg->hs_state & EEPROM_HS_SC) {
        Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 0U);
    } else {
        Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 0U);
    }

    if (cfg->hs_state & EEPROM_HS_VBUCK) {
        Enable_12V_Buck_Converter();
    } else {
        Disable_12V_Buck_Converter();
    }

    /* ── Fan speeds ── */
    set_Fan_PWM(FAN_DR, cfg->fan_dr);
    set_Fan_PWM(FAN_EP, cfg->fan_ep);
    set_Fan_PWM(FAN_EH, cfg->fan_eh);
    set_Fan_PWM(FAN_ST, cfg->fan_st);
    set_Fan_PWM(FAN_SF, cfg->fan_sf);
}

void EEPROM_SaveStartupConfig(void)
{
    EEPROM_StartupConfig_t cfg;
    cfg.magic = EEPROM_MAGIC;

    /* ── Snapshot HS GPIO enable pins ── */
    cfg.hs_state = 0U;
    if (HAL_GPIO_ReadPin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin) == GPIO_PIN_SET)
        cfg.hs_state |= EEPROM_HS_DR;
    if (HAL_GPIO_ReadPin(HS_E_EN_GPIO_Port, HS_E_EN_Pin) == GPIO_PIN_SET)
        cfg.hs_state |= EEPROM_HS_E;
    if (HAL_GPIO_ReadPin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin) == GPIO_PIN_SET)
        cfg.hs_state |= EEPROM_HS_SC;
    if (HAL_GPIO_ReadPin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin) == GPIO_PIN_SET)
        cfg.hs_state |= EEPROM_HS_VBUCK;

    /* ── Snapshot fan PWM setpoints from shadow array ── */
    cfg.fan_dr = get_Fan_PWM_Pct(FAN_DR);
    cfg.fan_ep = get_Fan_PWM_Pct(FAN_EP);
    cfg.fan_eh = get_Fan_PWM_Pct(FAN_EH);
    cfg.fan_st = get_Fan_PWM_Pct(FAN_ST);
    cfg.fan_sf = get_Fan_PWM_Pct(FAN_SF);

    cfg.checksum = compute_checksum(&cfg);

    EEPROM_Write(CFG_PAGE, CFG_OFFSET, (uint8_t *)&cfg, (uint16_t)sizeof(cfg));

    /* Update RAM cache */
    cached_cfg = cfg;
}

const EEPROM_StartupConfig_t *EEPROM_GetCachedConfig(void)
{
    return &cached_cfg;
}
