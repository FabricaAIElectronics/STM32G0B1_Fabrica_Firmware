/**
 * @file    eeprom_driver.c
 * @brief   I2C EEPROM driver and configuration storage.
 *
 * @author  jordan
 * @date    2025-12-01
 */

#include "eeprom_driver.h"
#include "CAN_Handler.h"
#include "main.h"
#include "fdcan.h"

#include <math.h>
#include <string.h>
#include <stdbool.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  EEPROM hardware constants
 * ════════════════════════════════════════════════════════════════════════════ */

#define EEPROM_I2C_ADDR     0xA0
#define PAGE_SIZE           64      /* bytes per page */
#define PAGE_COUNT          512     /* total pages */

extern I2C_HandleTypeDef hi2c1;

/* ════════════════════════════════════════════════════════════════════════════
 *  Cached config (RAM mirror of EEPROM page 0)
 * ════════════════════════════════════════════════════════════════════════════ */

static Config cached_cfg;
static bool   cached_valid = false;
static void (*config_update_cb)(const Config *cfg) = NULL;

/* ════════════════════════════════════════════════════════════════════════════
 *  Config defaults and validation
 * ════════════════════════════════════════════════════════════════════════════ */

void LoadDefault(Config *config)
{
    config->magic             = 0x3584;
    config->count             = 200;
    config->mode              = 0x01;
    config->pwm0              = 50;
    config->pwm1              = 30;
    config->hard_over_voltage = 26000;  /* mV */
    config->soft_over_voltage = 25500;  /* mV */
    config->under_voltage     = 16000;  /* mV */
    config->over_current      = 16000;  /* mA */
    config->fan_max_rpm       = 5000;
}

bool checkcfg(Config *cfg)
{
    return (cfg->magic == 0x3584);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Low-level EEPROM read / write (page-boundary aware)
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Calculate how many bytes can be written before hitting a page boundary.
 */
static uint16_t bytes_to_page_end(uint16_t size, uint16_t offset)
{
    uint16_t remaining_in_page = PAGE_SIZE - offset;
    return (size < remaining_in_page) ? size : remaining_in_page;
}

void EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size)
{
    int page_addr_bits = (int)(log(PAGE_SIZE) / log(2));
    uint16_t current_page = page;
    uint16_t pos = 0;

    while (size > 0) {
        uint16_t mem_addr     = (current_page << page_addr_bits) | offset;
        uint16_t chunk_size   = bytes_to_page_end(size, offset);

        if (HAL_I2C_Mem_Write(&hi2c1, EEPROM_I2C_ADDR, mem_addr, 2,
                              &data[pos], chunk_size, 1000) != HAL_OK) {
            return;
        }

        /* Wait for EEPROM internal write cycle to complete */
        for (int retry = 0; retry < 10; ++retry) {
            if (HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDR, 1, 100) == HAL_OK) {
                break;
            }
        }

        current_page++;
        offset = 0;             /* subsequent pages start at offset 0 */
        size  -= chunk_size;
        pos   += chunk_size;
    }
}

void EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size)
{
    int page_addr_bits = (int)(log(PAGE_SIZE) / log(2));
    uint16_t current_page = page;
    uint16_t pos = 0;

    while (size > 0) {
        uint16_t mem_addr   = (current_page << page_addr_bits) | offset;
        uint16_t chunk_size = bytes_to_page_end(size, offset);

        HAL_I2C_Mem_Read(&hi2c1, EEPROM_I2C_ADDR, mem_addr, 2,
                         &data[pos], chunk_size, 1000);

        current_page++;
        offset = 0;
        size  -= chunk_size;
        pos   += chunk_size;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Float ↔ byte conversion utilities
 * ════════════════════════════════════════════════════════════════════════════ */

uint32_t float2Bytes(float float_data)
{
    uint32_t raw;
    memcpy(&raw, &float_data, 4);
    return raw;
}

float bytes2Float(uint8_t buffer[4])
{
    float f;
    uint32_t raw = ((uint32_t)buffer[3] << 24) | ((uint32_t)buffer[2] << 16) |
                   ((uint32_t)buffer[1] << 8)  | ((uint32_t)buffer[0]);
    memcpy(&f, &raw, 4);
    return f;
}

void EEPROM_Write_Num(uint16_t page, uint16_t offset, float data)
{
    uint32_t raw = float2Bytes(data);
    uint8_t buffer[4] = {
        (uint8_t)(raw & 0xFF),
        (uint8_t)((raw >> 8) & 0xFF),
        (uint8_t)((raw >> 16) & 0xFF),
        (uint8_t)((raw >> 24) & 0xFF)
    };
    EEPROM_Write(page, offset, buffer, 4);
}

float EEPROM_Read_Num(uint16_t page, uint16_t offset)
{
    uint8_t buffer[4];
    EEPROM_Read(page, offset, buffer, 4);
    return bytes2Float(buffer);
}

void EEPROM_pageErase(uint16_t page)
{
    int page_addr_bits = (int)(log(PAGE_SIZE) / log(2));
    uint16_t mem_addr = page << page_addr_bits;

    uint8_t blank[PAGE_SIZE];
    memset(blank, 0xFF, PAGE_SIZE);

    HAL_I2C_Mem_Write(&hi2c1, EEPROM_I2C_ADDR, mem_addr, 2, blank, PAGE_SIZE, 1000);
    HAL_Delay(5);

    for (int retry = 0; retry < 10; ++retry) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDR, 1, 100) == HAL_OK) {
            break;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Config read / write with cache update
 * ════════════════════════════════════════════════════════════════════════════ */

void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg)
{
    EEPROM_Write(page, offset, (uint8_t *)cfg, sizeof(Config));

    /* Update RAM cache */
    cached_cfg   = *cfg;
    cached_valid = true;
    if (config_update_cb) {
        config_update_cb(&cached_cfg);
    }
}

void EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg)
{
    EEPROM_Read(page, offset, (uint8_t *)cfg, sizeof(Config));
}

const Config *EEPROM_GetCachedConfig(void)
{
    return cached_valid ? &cached_cfg : NULL;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Config sanitization — clamp out-of-range fields to defaults
 * ════════════════════════════════════════════════════════════════════════════ */

static bool EEPROM_SanitizeConfig(Config *cfg)
{
    Config defaults;
    LoadDefault(&defaults);
    bool repaired = false;

    if (cfg->hard_over_voltage < 10000 || cfg->hard_over_voltage > 60000) {
        cfg->hard_over_voltage = defaults.hard_over_voltage;
        repaired = true;
    }

    if (cfg->soft_over_voltage < 10000 || cfg->soft_over_voltage > 60000 ||
        cfg->soft_over_voltage >= cfg->hard_over_voltage) {
        cfg->soft_over_voltage = defaults.soft_over_voltage;
        repaired = true;
    }

    if (cfg->under_voltage < 5000 || cfg->under_voltage > 30000 ||
        cfg->under_voltage >= cfg->soft_over_voltage) {
        cfg->under_voltage = defaults.under_voltage;
        repaired = true;
    }

    if (cfg->over_current < 500 || cfg->over_current > 60000) {
        cfg->over_current = defaults.over_current;
        repaired = true;
    }

    if (cfg->fan_max_rpm < 100 || cfg->fan_max_rpm > 30000) {
        cfg->fan_max_rpm = defaults.fan_max_rpm;
        repaired = true;
    }

    if (cfg->count == 0 || cfg->count > 10000) {
        cfg->count = defaults.count;
        repaired = true;
    }

    if (cfg->pwm0 > 100) {
        cfg->pwm0 = defaults.pwm0;
        repaired = true;
    }

    if (cfg->pwm1 > 100) {
        cfg->pwm1 = defaults.pwm1;
        repaired = true;
    }

    return repaired;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  EEPROM initialization
 * ════════════════════════════════════════════════════════════════════════════ */

void EEPROM_Init(void)
{
    const uint16_t CFG_PAGE   = 0;
    const uint16_t CFG_OFFSET = 0;

    Config cfg;
    EEPROM_Read_Config(CFG_PAGE, CFG_OFFSET, &cfg);

    /* If magic is invalid, write factory defaults */
    if (!checkcfg(&cfg)) {
        LoadDefault(&cfg);
        EEPROM_Write_Config(CFG_PAGE, CFG_OFFSET, &cfg);

        /* Verify the write */
        Config verify;
        EEPROM_Read_Config(CFG_PAGE, CFG_OFFSET, &verify);
        if (checkcfg(&verify)) {
            cached_cfg   = verify;
            cached_valid = true;
            if (config_update_cb) config_update_cb(&cached_cfg);
        } else {
            cached_valid = false;
        }
        return;
    }

    /* Magic is valid — sanitize individual fields if needed */
    if (EEPROM_SanitizeConfig(&cfg)) {
        EEPROM_Write_Config(CFG_PAGE, CFG_OFFSET, &cfg);
    }

    cached_cfg   = cfg;
    cached_valid = true;
    if (config_update_cb) config_update_cb(&cached_cfg);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN command handler: write a single config setting
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_EEPROM_Write_Config(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id;

    if (params == NULL || len < 3) return;

    uint8_t  setting = params[0];
    uint16_t value   = (uint16_t)params[1] | ((uint16_t)params[2] << 8);

    if (value == 0) return;  /* reject zero values */

    /* Read existing config to preserve other fields */
    Config cfg;
    EEPROM_Read_Config(0, 0, &cfg);
    if (!checkcfg(&cfg)) {
        LoadDefault(&cfg);
    }

    switch (setting) {
    case EEPROM_SETTING_HARD_OV:
        if (value < 10000 || value > 60000) return;
        cfg.hard_over_voltage = value;
        break;
    case EEPROM_SETTING_SOFT_OV:
        if (value < 10000 || value > 60000) return;
        if (value >= cfg.hard_over_voltage) return;
        cfg.soft_over_voltage = value;
        break;
    case EEPROM_SETTING_UNDER_V:
        if (value < 5000 || value > 30000) return;
        if (value >= cfg.soft_over_voltage) return;
        cfg.under_voltage = value;
        break;
    case EEPROM_SETTING_OVER_CURR:
        if (value < 500 || value > 60000) return;
        cfg.over_current = value;
        break;
    case EEPROM_SETTING_FAN_MAX_RPM:
        if (value < 100 || value > 30000) return;
        cfg.fan_max_rpm = value;
        break;
    default:
        return;
    }

    EEPROM_Write_Config(0, 0, &cfg);

    /* ACK: echo back the setting + written value */
    uint8_t ack[3] = {
        setting,
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };
    FDCAN_SendFrame(CAN_ID_EEPROM_ACK, ack, 3);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN command handler: read and broadcast all config settings
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_EEPROM_Read_Config(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id; (void)params; (void)len;

    const Config *cfgp = EEPROM_GetCachedConfig();
    Config local_cfg;

    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &local_cfg);
        if (checkcfg(&local_cfg)) {
            cfgp = &local_cfg;
        }
    }

    /* Frame 1 (CAN_ID_EEPROM_ACK): hard OV, soft OV, overcurrent, fan max RPM */
    {
        uint8_t payload[8] = {0};
        if (cfgp != NULL) {
            payload[0] = (uint8_t)(cfgp->hard_over_voltage & 0xFF);
            payload[1] = (uint8_t)((cfgp->hard_over_voltage >> 8) & 0xFF);
            payload[2] = (uint8_t)(cfgp->soft_over_voltage & 0xFF);
            payload[3] = (uint8_t)((cfgp->soft_over_voltage >> 8) & 0xFF);
            payload[4] = (uint8_t)(cfgp->over_current & 0xFF);
            payload[5] = (uint8_t)((cfgp->over_current >> 8) & 0xFF);
            payload[6] = (uint8_t)(cfgp->fan_max_rpm & 0xFF);
            payload[7] = (uint8_t)((cfgp->fan_max_rpm >> 8) & 0xFF);
        }
        FDCAN_SendFrame(CAN_ID_EEPROM_ACK, payload, 8);
    }

    /* Frame 2 (CAN_ID_EEPROM_ACK2): under voltage */
    {
        uint8_t payload[2] = {0};
        if (cfgp != NULL) {
            payload[0] = (uint8_t)(cfgp->under_voltage & 0xFF);
            payload[1] = (uint8_t)((cfgp->under_voltage >> 8) & 0xFF);
        }
        FDCAN_SendFrame(CAN_ID_EEPROM_ACK2, payload, 2);
    }
}

/* Legacy stub */
void CAN_Handler_EEPROM_Write_Overvoltage_Config(uint32_t id, uint8_t *params, uint8_t len)
{
    CAN_Handler_EEPROM_Write_Config(id, params, len);
}
