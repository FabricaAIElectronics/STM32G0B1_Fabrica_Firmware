/*
 * powerstage_app.h
 *
 *  Created on: 15 Mar 2026
 *      Author: jordan
 *
 *  Application-level state machine for PowerStage firmware.
 *
 *  States:
 *    APP_STATE_INIT    — one-shot hardware / EEPROM / display setup
 *    APP_STATE_RUNNING — normal measurement, CAN broadcast, fan control
 *    APP_STATE_FAULT   — one or more ERR_xxx bits active; monitors for recovery
 *
 *  Transitions:
 *    INIT    → RUNNING  : init sequence complete
 *    RUNNING → FAULT    : display_data.error_code != ERR_NONE
 *    FAULT   → RUNNING  : display_data.error_code == ERR_NONE (all faults cleared)
 *
 *  Usage (main.c):
 *    // USER CODE BEGIN 2
 *    AppInit();          // OpenBLT bootloader hook
 *    PS_App_Init();      // PowerStage peripheral + CAN init
 *
 *    // while (1)
 *    AppTask();          // OpenBLT bootloader hook
 *    PS_App_Task();      // PowerStage state machine
 */

#ifndef INC_POWERSTAGE_APP_H_
#define INC_POWERSTAGE_APP_H_

#include "main.h"
#include <stdbool.h>

/* ============================================================
 * Application state machine
 * ============================================================ */
typedef enum {
    APP_STATE_INIT    = 0,  /* one-shot init — runs first iteration only */
    APP_STATE_RUNNING,      /* normal operation                          */
    APP_STATE_FAULT         /* fault(s) active                           */
} AppState_t;

/* ============================================================
 * Exported state (read-only outside this module)
 * ============================================================ */
extern AppState_t g_app_state;
extern bool       g_oled_present;
extern bool       g_eeprom_present;

/* ============================================================
 * API
 * ============================================================ */

/* Call once in main() after all MX_xxx_Init() and AppInit() */
void PS_App_Init(void);

/* Call every main-loop cycle — drives the state machine */
void PS_App_Task(void);

#endif /* INC_POWERSTAGE_APP_H_ */
