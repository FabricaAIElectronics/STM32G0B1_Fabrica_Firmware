/**
 * @file    applogic.c
 * @brief   KincoDrive low-level driver state machine.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "applogic.h"
#include "main.h"
#include "adc_driver.h"
#include "Fan_PWM.h"
#include "CAN_Handler.h"
#include "eeprom_driver.h"
#include "power_monitor.h"

#include <string.h>

/* Tick periods */
#define PROTECTION_PERIOD_MS    50U
#define BROADCAST_PERIOD_MS     500U

extern FDCAN_HandleTypeDef hfdcan1;

/* ────────── Private state handlers ────────── */

static void state_init(AppStateMachine *sm)
{


    /* ADC: calibrate then start continuous DMA */
    Calibrate_ADC1();
    Start_ADC1_DMA();

    /* Fan PWM + tachometer DMA */
    start_all_Fan_PWM();
    start_Fan_Tacho_DMA();

    /* FDCAN start + filter */
    HAL_FDCAN_Start(&hfdcan1);
    CAN_Handler_Init();


    uint32_t now = HAL_GetTick();
    sm->last_protection_tick = now;
    sm->last_broadcast_tick  = now;

    sm->state = APP_STATE_LOAD_CONFIG;
}

static void state_load_config(AppStateMachine *sm)
{
    /* Read config from EEPROM into RAM cache (or fall back to safe defaults). */
    EEPROM_Init();

    /* Apply boot HS state + fan defaults + OC/UV thresholds.
     * Per user requirement #4: maintain bootHS state from EEPROM so that
     * the EEPROM round-trip is exercised on every boot. */
    EEPROM_ApplyStartupConfig();

    sm->state = APP_STATE_RUNNING;
}

static void state_running(AppStateMachine *sm)
{
    uint32_t now = HAL_GetTick();

    /* ── Continuous: handle pending CAN commands ── */
    CAN_Process();

    /* ── 50 ms: protection (OC trip + UV detect) ── */
    if ((now - sm->last_protection_tick) >= PROTECTION_PERIOD_MS) {
        sm->last_protection_tick = now;
        (void)PM_RunProtection();
    }

    /* ── 500 ms (staggered internally over 3 phases): telemetry ── */
    if ((now - sm->last_broadcast_tick) >= BROADCAST_PERIOD_MS) {
        sm->last_broadcast_tick = now;
        HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    }
    /* CAN_Broadcast handles its own internal 3-phase scheduling; call every loop. */
    CAN_Broadcast(BROADCAST_PERIOD_MS);
}

/* ────────── Public API ────────── */

void AppLogic_Init(AppStateMachine *sm)
{
    memset(sm, 0, sizeof(*sm));
    sm->state = APP_STATE_INIT;

    /* Run INIT and LOAD_CONFIG synchronously here so that AppLogic_Run() in
     * the main loop only sees STATE_RUNNING. This matches how main.c is
     * structured (single AppLogic_Init call before the loop). */
    state_init(sm);
    state_load_config(sm);
}

void AppLogic_Run(AppStateMachine *sm)
{
    switch (sm->state) {
    case APP_STATE_INIT:        state_init(sm);        break;
    case APP_STATE_LOAD_CONFIG: state_load_config(sm); break;
    case APP_STATE_RUNNING:     state_running(sm);     break;
    default:                    sm->state = APP_STATE_INIT; break;
    }
}

AppState_t AppLogic_GetState(const AppStateMachine *sm)
{
    return sm->state;
}
