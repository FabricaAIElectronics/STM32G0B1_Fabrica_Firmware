/**
 * @file    AppLogic.c
 * @brief   Top-level state machine and config dispatcher.
 *
 * @details See AppLogic.h for dataflow overview.
 *
 * @author  jordan
 * @date    2026-04-21
 */

#include "AppLogic.h"
#include "CAN_Handler.h"
#include "eeprom_driver.h"
#include "Power_Electronic.h"
#include "Fan_PWM.h"
#include "main.h"

#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Private state
 * ═══════════════════════════════════════════════════════════════════════ */

static Config_t   app_cfg;
static AppState_t app_state = APP_INIT;

/* ═══════════════════════════════════════════════════════════════════════
 *  Defaults  (used when EEPROM is blank/corrupt or on "load defaults" cmd)
 * ═══════════════════════════════════════════════════════════════════════ */

static void app_defaults(Config_t *c)
{
    c->hs_state = 0U;    /* all HS OFF, buck OFF */
    c->fan_dr   = 0U;
    c->fan_ep   = 0U;
    c->fan_eh   = 0U;
    c->fan_st   = 0U;
    c->fan_sf   = 0U;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Hardware actuation  (only place that writes GPIOs / PWMs)
 * ═══════════════════════════════════════════════════════════════════════ */

static void apply_hs(uint8_t hs)
{
    if (hs & CFG_HS_DR)    Enable_HighSide_Power_Module(HS_MODULE_DRIVE);
    else                   Disable_HighSide_Power_Module(HS_MODULE_DRIVE);

    if (hs & CFG_HS_E)     Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER);
    else                   Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER);

    if (hs & CFG_HS_SC)    Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING);
    else                   Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING);

    if (hs & CFG_HS_VBUCK) Enable_12V_Buck_Converter();
    else                   Disable_12V_Buck_Converter();
}

static void apply_fans(const Config_t *c)
{
    set_Fan_PWM(FAN_DR, c->fan_dr);
    set_Fan_PWM(FAN_EP, c->fan_ep);
    set_Fan_PWM(FAN_EH, c->fan_eh);
    set_Fan_PWM(FAN_ST, c->fan_st);
    set_Fan_PWM(FAN_SF, c->fan_sf);
}

static void apply_all(const Config_t *c)
{
    apply_hs(c->hs_state);
    apply_fans(c);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CAN command dispatch
 * ═══════════════════════════════════════════════════════════════════════ */

static void handle_can(void)
{
    CAN_RxFrame_t f;
    if (!CAN_TryGetFrame(&f))
        return;

    switch (f.id) {

    case MSG_CMD_HS_POWER:
        /* DLC=1.  data[0] = HS/buck bitmask (see Config_t.hs_state). */
        if (f.dlc >= 1U) {
            app_cfg.hs_state = f.data[0] & 0x0FU;
            apply_hs(app_cfg.hs_state);
        }
        break;

    case MSG_CMD_FAN_PWM:
        /* DLC=5.  data[0..4] = DR / EP / EH / ST / SF (0–100 %). */
        if (f.dlc >= 5U) {
            app_cfg.fan_dr = f.data[0];
            app_cfg.fan_ep = f.data[1];
            app_cfg.fan_eh = f.data[2];
            app_cfg.fan_st = f.data[3];
            app_cfg.fan_sf = f.data[4];
            apply_fans(&app_cfg);
        }
        break;

    case MSG_CMD_EEPROM:
        /* DLC=1.  0 = load hard-coded defaults and apply.
         *         1 = save current live state to EEPROM. */
        if (f.dlc >= 1U) {
            if (f.data[0] == 1U) {
                EEPROM_Save(&app_cfg);
            } else {
                app_defaults(&app_cfg);
                apply_all(&app_cfg);
            }
        }
        break;

    default:
        /* unknown ID — ignore */
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

void App_Init(void)
{
    /* Pull persisted config from EEPROM (or safe defaults if corrupt). */
    if (!EEPROM_Load(&app_cfg)) {
        app_defaults(&app_cfg);
    }

    /* Push live state to hardware. */
    apply_all(&app_cfg);

    app_state = APP_RUN;
}

void App_Run(void)
{
    switch (app_state) {

    case APP_INIT:
        App_Init();
        break;

    case APP_RUN:
        handle_can();
        CAN_Broadcast(500U);
        break;

    case APP_ERROR:
    default:
        /* Skeleton: outputs off, slow LED blink.  No auto-recovery. */
        {
            static uint32_t err_tick = 0;
            uint32_t now = HAL_GetTick();
            if ((now - err_tick) >= 250U) {
                err_tick = now;
                HAL_GPIO_TogglePin(LED_OUT_GPIO_Port, LED_OUT_Pin);
            }
        }
        break;
    }
}

const Config_t *App_GetConfig(void)
{
    return &app_cfg;
}
