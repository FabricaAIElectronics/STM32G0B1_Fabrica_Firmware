/**
 * @file    applogic.h
 * @brief   Top-level state machine for KincoDrive low-level driver.
 *
 * @details Simple linear state machine focused on hardware bring-up:
 *
 *            STATE_INIT         (one-shot peripheral init)
 *               │
 *               ▼
 *            STATE_LOAD_CONFIG  (read EEPROM, apply boot HS state + thresholds)
 *               │
 *               ▼
 *            STATE_RUNNING      (periodic protection, broadcasts, command processing)
 *
 *          There is NO ERROR/RECOVERY state — protection events update an
 *          error mask in power_monitor that is broadcast every cycle. The
 *          system continues to run; the host is the policy decider.
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef APPLOGIC_H
#define APPLOGIC_H

#include <stdint.h>

typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_LOAD_CONFIG,
    APP_STATE_RUNNING
} AppState_t;

typedef struct {
    AppState_t state;

    /* Non-blocking tick timestamps */
    uint32_t   last_protection_tick;
    uint32_t   last_broadcast_tick;
} AppStateMachine;

/** Initialize state machine struct and set initial state to APP_STATE_INIT. */
void AppLogic_Init(AppStateMachine *sm);

/** Run one iteration. Call from main while(1). */
void AppLogic_Run(AppStateMachine *sm);

/** Return current state (used by CAN status broadcast). */
AppState_t AppLogic_GetState(const AppStateMachine *sm);

#endif /* APPLOGIC_H */
