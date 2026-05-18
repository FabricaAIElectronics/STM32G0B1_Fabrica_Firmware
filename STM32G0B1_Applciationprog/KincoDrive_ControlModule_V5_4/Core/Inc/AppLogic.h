/**
 * @file    AppLogic.h
 * @brief   Top-level application state machine and master configuration.
 *
 * @details AppLogic owns the single source of truth for live hardware state
 *          (high-side switches, fan setpoints).  CAN_Handler and eeprom_driver
 *          are dumb data layers — they never touch GPIOs or PWMs directly.
 *
 *          Dataflow:
 *            boot         EEPROM ──► app_cfg ──► HW
 *            CAN command  CAN_RxFrame ──► app_cfg ──► HW
 *            save         app_cfg ──► EEPROM
 *            load default defaults ──► app_cfg ──► HW
 *
 * @author  jordan
 * @date    2026-04-21
 */

#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Shared configuration type
 *
 *  Instances:
 *    - AppLogic   : live state (master)
 *    - eeprom_drv : persisted startup copy
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct __attribute__((packed)) {
    uint8_t hs_state;   /* bit0=DR  bit1=E  bit2=SC  bit3=VBUCK   (1=ON) */
    uint8_t fan_dr;     /* 0–100 %                                        */
    uint8_t fan_ep;     /* 0–100 %                                        */
    uint8_t fan_eh;     /* 0–100 %                                        */
    uint8_t fan_st;     /* 0–100 %                                        */
    uint8_t fan_sf;     /* 0–100 %                                        */
} Config_t;

/* hs_state bitmask helpers */
#define CFG_HS_DR       (1U << 0)
#define CFG_HS_E        (1U << 1)
#define CFG_HS_SC       (1U << 2)
#define CFG_HS_VBUCK    (1U << 3)

/* ═══════════════════════════════════════════════════════════════════════
 *  State machine
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum {
    APP_INIT = 0,
    APP_RUN,
    APP_ERROR
} AppState_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

/** One-shot init: load EEPROM → app_cfg → apply to HW → enter RUN. */
void App_Init(void);

/** Tick once per main-loop iteration: service CAN + broadcast telemetry. */
void App_Run(void);

/** Read-only access to the live config (for broadcasts / debug). */
const Config_t *App_GetConfig(void);

#endif /* APP_LOGIC_H */
