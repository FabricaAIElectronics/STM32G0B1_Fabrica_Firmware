/*
 * ui_display.h
 *
 *  Created on: 15 Mar 2026
 *      Author: jordan
 *
 *  Screen : SSD1306 128 × 64 px (2.42")
 *  Note   : User specified 128×72 — SSD1306_HEIGHT is defined as 64 in
 *           ssd1306.h.  If your module uses a 128×72 controller, update
 *           SSD1306_HEIGHT there and this module adapts automatically.
 *
 *  Pages  :
 *    Page 0 — System Overview   (state, voltages, all currents)
 *    Page 1 — Rail Status        (PG / FLT / OC per HS rail)
 *    Page 2 — Fault Detail       (UV / OC / HW-FLT breakdown)
 *
 *  Usage  :
 *    UI_Display_Init();                         // once at startup
 *    UI_Display_UpdateFromModules(&ms, &fan);   // call each main-loop cycle
 *    DisplayScheduler_Task();                   // drives 500 ms refresh + page cycle
 */

#ifndef INC_UI_DISPLAY_H_
#define INC_UI_DISPLAY_H_

#include "main.h"
#include "io_module.h"
#include "fan_ctrl.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * System state enum
 * ============================================================ */
typedef enum {
    SYS_INIT     = 0,
    SYS_RUNNING,
    SYS_FAULT,
    SYS_STANDBY
} SystemState_t;

/* ============================================================
 * Error code bitmask  (uint16_t)
 * Build by OR-ing the defines below, store in display_data.error_code
 * ============================================================ */
#define ERR_NONE            0x0000

/* Undervoltage (from uv_status.uv_fault_mask) */
#define ERR_UV_V24          (1u <<  0)
#define ERR_UV_VCAP         (1u <<  1)
#define ERR_UV_V12          (1u <<  2)

/* Software OC warning (from oc_status.oc_warn_mask) */
#define ERR_OC_AUX          (1u <<  3)
#define ERR_OC_LED          (1u <<  4)
#define ERR_OC_DRIVE        (1u <<  5)
#define ERR_OC_CAP          (1u <<  6)
#define ERR_OC_SBC          (1u <<  7)

/* Hardware FLT pin (from oc_status.oc_fault_mask / TPS2493) */
#define ERR_HS_FLT_AUX      (1u <<  8)
#define ERR_HS_FLT_LED      (1u <<  9)
#define ERR_HS_FLT_DRV      (1u << 10)
#define ERR_HS_FLT_CAP      (1u << 11)
#define ERR_HS_FLT_SBC      (1u << 12)

/* Thermal */
#define ERR_OVERHEAT        (1u << 13)

/* ============================================================
 * Display page cycle
 * ============================================================ */
typedef enum {
    PAGE_OVERVIEW    = 0,
    PAGE_RAIL_STATUS,
    PAGE_FAULT_DETAIL,
    PAGE_COUNT
} DisplayPage_t;

/* Dwell time per page in display_scheduler ticks (500 ms each)
 * PAGE_DWELL 10 → 5 seconds per page                         */
#define PAGE_DWELL          10

/* ============================================================
 * Overheat threshold (°C)
 * ============================================================ */
#define UI_OVERHEAT_TEMP_C  80.0f

/* ============================================================
 * Display data struct — central data store for rendering
 * Populate via UI_Display_UpdateFromModules() each main cycle
 * ============================================================ */
typedef struct {
    /* System */
    SystemState_t   state;
    uint16_t        error_code;         // ERR_xxx bitmask

    /* Voltages (mV) */
    uint16_t        v24_mV;
    uint16_t        vcap_mV;
    uint16_t        v12_mV;
    float           temp_C;

    /* Currents (mA) */
    uint16_t        i_bat_mA;
    uint16_t        i_cap_mA;
    uint16_t        i_sbc_mA;
    uint16_t        i_drive_mA;
    uint16_t        i_aux_mA;
    uint16_t        i_led_mA;

    /* Fan */
    uint8_t         fan_mode;           // 0=OFF, 1=ON, 2=AUTO
    uint8_t         fan_duty;           // 0–100 %

    /* HS rail status bitmasks (bit n = RAIL index n) */
    uint8_t         hs_pgood_mask;
    uint8_t         hs_fault_mask;      // TPS2493 HW FLT
    uint8_t         hs_oc_warn_mask;    // software OC warning
    uint8_t         uv_fault_mask;      // UV_FAULT_V24/VCAP/V12
} DisplayData_t;

/* ============================================================
 * Extern
 * ============================================================ */
extern DisplayData_t display_data;

/* ============================================================
 * API
 * ============================================================ */

/* Call once at startup after SSD1306_Init() */
void UI_Display_Init(void);

/* Copy live data from io_module + fan_ctrl into display_data
 * and rebuild error_code.  Call each main-loop cycle BEFORE
 * DisplayScheduler_Task().                                    */
void UI_Display_UpdateFromModules(SystemMeasurement_t *ms, FanCTRL_t *fan);

/* Manually override system state (e.g. during init sequence) */
void UI_Display_SetState(SystemState_t state);

/* Manually set the error code (overrides auto-build from modules) */
void UI_Display_SetErrorCode(uint16_t error_code);

/* Force an immediate screen redraw without waiting for scheduler */
void UI_Display_Update(void);

/* ============================================================
 * display_scheduler callback — do NOT call directly.
 * Implement here; called automatically by DisplayScheduler_Task().
 * ============================================================ */
void DisplayContent_Update(void);

#endif /* INC_UI_DISPLAY_H_ */
