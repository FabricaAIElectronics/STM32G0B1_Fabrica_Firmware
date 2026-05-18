/**
 * @file    hs_switch.c
 * @brief   High-side switch + 12 V buck digital output control.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "hs_switch.h"
#include "main.h"

/* Per-module GPIO mapping for EN, PGOOD, FAULT pins. */
typedef struct {
    GPIO_TypeDef *en_port;   uint16_t en_pin;
    GPIO_TypeDef *pg_port;   uint16_t pg_pin;
    GPIO_TypeDef *ft_port;   uint16_t ft_pin;
} HS_Pinmap_t;

static const HS_Pinmap_t hs_pin[HS_MODULE_COUNT] = {
    [HS_MODULE_DRIVE] = {
        .en_port = HS_DR_EN_GPIO_Port, .en_pin = HS_DR_EN_Pin,
        .pg_port = HS_DR_PG_GPIO_Port, .pg_pin = HS_DR_PG_Pin,
        .ft_port = HS_DR_FT_GPIO_Port, .ft_pin = HS_DR_FT_Pin,
    },
    [HS_MODULE_EXTRUDER] = {
        .en_port = HS_E_EN_GPIO_Port,  .en_pin = HS_E_EN_Pin,
        .pg_port = HS_E_PG_GPIO_Port,  .pg_pin = HS_E_PG_Pin,
        .ft_port = HS_E_FT_GPIO_Port,  .ft_pin = HS_E_FT_Pin,
    },
    [HS_MODULE_SCRUBBING] = {
        .en_port = HS_SC_EN_GPIO_Port, .en_pin = HS_SC_EN_Pin,
        .pg_port = HS_SC_PG_GPIO_Port, .pg_pin = HS_SC_PG_Pin,
        .ft_port = HS_SC_FT_GPIO_Port, .ft_pin = HS_SC_FT_Pin,
    },
};

/* ────────── HS channel control ────────── */

int HS_Enable(HS_Module_t module)
{
    if (module >= HS_MODULE_COUNT) return HS_ERR_INVALID_PARAM;
    HAL_GPIO_WritePin(hs_pin[module].en_port, hs_pin[module].en_pin, GPIO_PIN_SET);
    return HS_SUCCESS;
}

int HS_Disable(HS_Module_t module)
{
    if (module >= HS_MODULE_COUNT) return HS_ERR_INVALID_PARAM;
    HAL_GPIO_WritePin(hs_pin[module].en_port, hs_pin[module].en_pin, GPIO_PIN_RESET);
    return HS_SUCCESS;
}

bool HS_IsEnabled(HS_Module_t module)
{
    if (module >= HS_MODULE_COUNT) return false;
    return HAL_GPIO_ReadPin(hs_pin[module].en_port, hs_pin[module].en_pin) == GPIO_PIN_SET;
}

bool HS_PowerGood(HS_Module_t module)
{
    /* PGOOD on TPS2493 is active LOW; pull-up means HIGH = not good. */
    if (module >= HS_MODULE_COUNT) return false;
    return HAL_GPIO_ReadPin(hs_pin[module].pg_port, hs_pin[module].pg_pin) == GPIO_PIN_RESET;
}

bool HS_FaultPin(HS_Module_t module)
{
    /* FAULT on TPS2493 is active LOW; pull-up means LOW = fault asserted. */
    if (module >= HS_MODULE_COUNT) return false;
    return HAL_GPIO_ReadPin(hs_pin[module].ft_port, hs_pin[module].ft_pin) == GPIO_PIN_RESET;
}

/* ────────── 12 V buck ────────── */

void Buck12V_Enable(void)
{
    HAL_GPIO_WritePin(VBUCK_EN_GPIO_Port, VBUCK_EN_Pin, GPIO_PIN_SET);
}

void Buck12V_Disable(void)
{
    HAL_GPIO_WritePin(VBUCK_EN_GPIO_Port, VBUCK_EN_Pin, GPIO_PIN_RESET);
}

bool Buck12V_IsEnabled(void)
{
    return HAL_GPIO_ReadPin(VBUCK_EN_GPIO_Port, VBUCK_EN_Pin) == GPIO_PIN_SET;
}
