/*
 * ui_display.c
 *
 *  Created on: 15 Mar 2026
 *      Author: jordan
 *
 *  Screen layout — Font_7x10 (7 px wide × 10 px tall)
 *  Row spacing   — SSD1306_HEIGHT / 6 pixels
 *  Max chars/row — 128 / 7 = 18 characters
 *
 *  ╔══════════════════╗  Page 0 — Overview
 *  ║ST:RUN  ERR:0x0000║  row 0  state + hex error code
 *  ║V24:24.1V VC:23.8V║  row 1  V24 + VCAP
 *  ║V12:12.0V  T:45.2C║  row 2  V12 + NTC temperature
 *  ║BAT:1250 CAP: 850 ║  row 3  BAT + CAP current (mA)
 *  ║DRV: 450 SBC: 300 ║  row 4  DRIVE + SBC current
 *  ║AUX: 200 LED: 150 ║  row 5  AUX + LED current
 *  ╚══════════════════╝
 *
 *  ╔══════════════════╗  Page 1 — Rail Status
 *  ║RAIL  PG  FLT  OC ║  row 0  column headers
 *  ║AUX    Y    N   N ║  row 1
 *  ║LED    Y    N   N ║  row 2
 *  ║DRV    Y    N   N ║  row 3
 *  ║CAP    Y    N   N ║  row 4
 *  ║SBC    □    N   N ║  row 5  blank squares — no MCU EN disabled
 *  ╚══════════════════╝
 *  PG=PowerGood  FLT=HW fault  OC=software OC warning
 *
 *  ╔══════════════════╗  Page 2 — Fault Detail
 *  ║--FAULT DETAIL----║  row 0  title
 *  ║UV : V24  VCP  V12║  row 1  active UV rails (blank if OK)
 *  ║OC : AUX  LED  DRV║  row 2  active OC rails
 *  ║     CAP  SBC     ║  row 3  (continuation)
 *  ║FLT: AUX  LED  DRV║  row 4  active HW faults
 *  ║     CAP  SBC OVH ║  row 5  (cont.) + overheat flag
 *  ╚══════════════════╝
 *  When no faults: rows 1-5 replaced with "   NO ACTIVE FAULTS"
 *
 *  NOTE: Float formatting uses integer arithmetic to avoid the
 *  -u _printf_float linker flag required by newlib-nano.
 */

#include "ui_display.h"
#include "ssd1306.h"
#include "fonts.h"
#include "can_operation.h"      /* oc_status, uv_status             */
#include "io_module.h"          /* hotswap[], HS_PGood, HS_Fault    */
#include "display_scheduler.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Layout helpers
 * ============================================================ */
#define UI_ROWS         6
#define UI_ROW_H        (SSD1306_HEIGHT / UI_ROWS)  /* 10 px for 64-high screen */
#define UI_ROW(n)       ((n) * UI_ROW_H)            /* Y pixel of row n         */

/* Column X positions for Page 1 (7 px per char) */
#define COL_RAIL        0
#define COL_PG          (5  * 7)    /* "RAIL " = 5 chars → pixel 35  */
#define COL_FLT         (8  * 7)    /* "RAIL  PG  " = 8+1 → pixel 56  */
#define COL_OC          (12 * 7)    /* pixel 84                      */

/* Column X positions for Page 2 fault labels */
#define COL_F_PREFIX    0
#define COL_F_1         (5  * 7)    /* first item  */
#define COL_F_2         (9  * 7)    /* second item */
#define COL_F_3         (13 * 7)    /* third item  */

/* ============================================================
 * Private helpers
 * ============================================================ */

/* Write a fixed 18-char row — pads with spaces to clear leftovers */
static inline void draw_row(uint8_t row, const char *str)
{
    char padded[20];
    snprintf(padded, sizeof(padded), "%-18s", str);
    SSD1306_GotoXY(0, UI_ROW(row));
    SSD1306_Puts(padded, &Font_7x10, SSD1306_COLOR_WHITE);
}

/* Place a single short string at exact pixel coordinates */
static inline void draw_at(uint16_t x, uint16_t y, const char *str)
{
    SSD1306_GotoXY(x, y);
    SSD1306_Puts(str, &Font_7x10, SSD1306_COLOR_WHITE);
}

/* Draw a 5×6 outlined rectangle centred in a 7×10 font cell.
 * Used as a placeholder for "not applicable / not controllable" — e.g.
 * the SBC row on Page 1 (no MCU EN, no software OC). */
static inline void draw_blank_box(uint16_t x, uint16_t y)
{
    SSD1306_DrawRectangle(x + 1, y + 2, 5, 6, SSD1306_COLOR_WHITE);
}

/* Format float as "±XXX.X" into buf (no %f needed) */
static void fmt_f1(char *buf, uint8_t bufsize, float val)
{
    int  sign = (val < 0.0f) ? -1 : 1;
    float abs_val = (val < 0.0f) ? -val : val;
    int  i = (int)abs_val;
    int  d = (int)((abs_val - (float)i) * 10.0f);
    if (sign < 0)
        snprintf(buf, bufsize, "-%d.%d", i, d);
    else
        snprintf(buf, bufsize, "%d.%d", i, d);
}

/* ============================================================
 * State strings (must match SystemState_t order)
 * ============================================================ */
static const char * const state_str[] = {
    "INIT",     /* SYS_INIT     */
    "RUN ",     /* SYS_RUNNING  */
    "FLT!",     /* SYS_FAULT    */
    "STBY"      /* SYS_STANDBY  */
};

/* Rail short labels (must match PowerRailIndex_t order) */
static const char * const rail_lbl[] = {
    "AUX", "LED", "DRV", "CAP", "SBC"
};

/* ============================================================
 * Exported display_data
 * ============================================================ */
DisplayData_t display_data = {0};

/* ============================================================
 * Private page draw functions
 * ============================================================ */

/* ----------------------------------------------------------
 * Page 0 — System Overview
 *
 *   ╔══════════════════╗
 *   ║ST:RUN  ERR:0x0000║  state + error code
 *   ║V24:24.1V  SOC: 75║  V24 + battery SOC % (NEW)
 *   ║V12:12.0V VC:23.8V║  V12 + VCAP (VC moved here)
 *   ║ T:45.2C BT:1.250A║  NTC temp + battery current
 *   ║DR:1.500A SB:0.300║  DRIVE + SBC currents
 *   ║AX:0.200A LD:0.150║  AUX + LED currents
 *   ╚══════════════════╝
 *
 *   CAP current dropped from this page — visible via the rail-status
 *   page and the BCAST_HS_CURR_A CAN frame. Battery SOC sits inline next
 *   to V24 because that's where the user is naturally looking for the
 *   "is the pack OK?" answer.
 * ---------------------------------------------------------- */
static void Draw_Page_Overview(void)
{
    char buf[26];
    char tmp[10];

    /* Row 0: State + error code */
    const char *sname = (display_data.state <= SYS_STANDBY)
                        ? state_str[display_data.state]
                        : "????";
    snprintf(buf, sizeof(buf), "ST:%-4s ERR:0x%04X", sname,
             (unsigned int)display_data.error_code);
    draw_row(0, buf);

    /* Row 1: V24 + battery SOC (next to V24 per user request) */
    fmt_f1(tmp, sizeof(tmp), display_data.v24_mV / 1000.0f);
    snprintf(buf, sizeof(buf), "V24:%-4sV  SOC:%3u",
             tmp, (unsigned)display_data.battery_soc_pct);
    draw_row(1, buf);

    /* Row 2: V12 + VCAP */
    fmt_f1(tmp, sizeof(tmp), display_data.v12_mV / 1000.0f);
    snprintf(buf, sizeof(buf), "V12:%-4sV", tmp);
    fmt_f1(tmp, sizeof(tmp), display_data.vcap_mV / 1000.0f);
    snprintf(buf + 9, sizeof(buf) - 9, "VC:%-4sV", tmp);
    draw_row(2, buf);

    /* Row 3: NTC temperature + battery current */
    fmt_f1(tmp, sizeof(tmp), display_data.temp_C);
    snprintf(buf, sizeof(buf), " T:%-4sC", tmp);
    fmt_f1(tmp, sizeof(tmp), display_data.i_bat_mA / 1000.0f);
    snprintf(buf + 8, sizeof(buf) - 8, " BT:%4sA", tmp);
    draw_row(3, buf);

    /* Row 4: DRIVE + SBC current */
    fmt_f1(tmp, sizeof(tmp), display_data.i_drive_mA / 1000.0f);
    snprintf(buf, sizeof(buf), "DR:%4sA", tmp);
    fmt_f1(tmp, sizeof(tmp), display_data.i_sbc_mA / 1000.0f);
    snprintf(buf + 8, sizeof(buf) - 8, " SB:%4sA", tmp);
    draw_row(4, buf);

    /* Row 5: AUX + LED current */
    fmt_f1(tmp, sizeof(tmp), display_data.i_aux_mA / 1000.0f);
    snprintf(buf, sizeof(buf), "AX:%4sA", tmp);
    fmt_f1(tmp, sizeof(tmp), display_data.i_led_mA / 1000.0f);
    snprintf(buf + 8, sizeof(buf) - 8, " LD:%4sA", tmp);
    draw_row(5, buf);
}

/* ----------------------------------------------------------
 * Page 1 — Rail Status
 * PG = PowerGood  |  FLT = hardware FLT pin  |  OC = SW OC warn
 * ---------------------------------------------------------- */
static void Draw_Page_RailStatus(void)
{
    /* Row 0: column headers using GotoXY for alignment */
    SSD1306_Fill(SSD1306_COLOR_BLACK);          /* already cleared in caller */

    draw_at(COL_RAIL, UI_ROW(0), "RAIL");
    draw_at(COL_PG,   UI_ROW(0), "PG");
    draw_at(COL_FLT,  UI_ROW(0), "FLT");
    draw_at(COL_OC,   UI_ROW(0), "OC");

    /* Rows 1-5: one per rail. RAIL_SBC has no MCU-controlled enable line,
     * its software OC threshold is forced to 0 (disabled), and it is
     * intentionally skipped by the CMD_HS handler, so we render blank
     * squares in place of Y/N to make the read-only nature visible. */
    for (uint8_t i = 0; i < RAIL_COUNT; i++) {
        uint8_t y = UI_ROW(i + 1);

        draw_at(COL_RAIL, y, rail_lbl[i]);

//        if (i == RAIL_SBC) {
//            draw_blank_box(COL_PG,  y);
//            draw_blank_box(COL_FLT, y);
//            draw_blank_box(COL_OC,  y);
//            continue;
//        }

        char ch_pg  = (display_data.hs_pgood_mask   & (1u << i)) ? 'Y' : 'N';
        char ch_flt = (display_data.hs_fault_mask   & (1u << i)) ? 'Y' : 'N';
        char ch_oc  = (display_data.hs_oc_warn_mask & (1u << i)) ? 'Y' : 'N';

        char single[2] = {0, 0};
        single[0] = ch_pg;  draw_at(COL_PG,  y, single);
        single[0] = ch_flt; draw_at(COL_FLT, y, single);
        single[0] = ch_oc;  draw_at(COL_OC,  y, single);
    }
}

/* ----------------------------------------------------------
 * Page 2 — Fault Detail
 * Labels shown only when the corresponding fault bit is set.
 * All-clear shows a single "NO ACTIVE FAULTS" message.
 * ---------------------------------------------------------- */
static void Draw_Page_FaultDetail(void)
{
    uint16_t err = display_data.error_code;

    /* Row 0: title */
    draw_row(0, "--FAULT DETAIL----");

    if (err == ERR_NONE) {
        /* Centre-ish "ALL CLEAR" message */
        draw_row(2, "  NO ACTIVE FAULTS");
        draw_row(3, "                  ");
        draw_row(4, "                  ");
        draw_row(5, "                  ");
        return;
    }

    /* Row 1: Undervoltage rail labels */
    draw_at(COL_F_PREFIX, UI_ROW(1), "UV :");
    draw_at(COL_F_1,      UI_ROW(1), (err & ERR_UV_V24)  ? "V24" : "   ");
    draw_at(COL_F_2,      UI_ROW(1), (err & ERR_UV_VCAP) ? "VCP" : "   ");
    draw_at(COL_F_3,      UI_ROW(1), (err & ERR_UV_V12)  ? "V12" : "   ");

    /* Row 2: OC warning first three rails */
    draw_at(COL_F_PREFIX, UI_ROW(2), "OC :");
    draw_at(COL_F_1,      UI_ROW(2), (err & ERR_OC_AUX)   ? "AUX" : "   ");
    draw_at(COL_F_2,      UI_ROW(2), (err & ERR_OC_LED)   ? "LED" : "   ");
    draw_at(COL_F_3,      UI_ROW(2), (err & ERR_OC_DRIVE) ? "DRV" : "   ");

    /* Row 3: OC warning remaining rails */
    draw_at(COL_F_PREFIX, UI_ROW(3), "    ");
    draw_at(COL_F_1,      UI_ROW(3), (err & ERR_OC_CAP) ? "CAP" : "   ");
    draw_at(COL_F_2,      UI_ROW(3), (err & ERR_OC_SBC) ? "SBC" : "   ");

    /* Row 4: HW FLT first three rails */
    draw_at(COL_F_PREFIX, UI_ROW(4), "FLT:");
    draw_at(COL_F_1,      UI_ROW(4), (err & ERR_HS_FLT_AUX) ? "AUX" : "   ");
    draw_at(COL_F_2,      UI_ROW(4), (err & ERR_HS_FLT_LED) ? "LED" : "   ");
    draw_at(COL_F_3,      UI_ROW(4), (err & ERR_HS_FLT_DRV) ? "DRV" : "   ");

    /* Row 5: HW FLT remaining rails + overheat */
    draw_at(COL_F_PREFIX, UI_ROW(5), "    ");
    draw_at(COL_F_1,      UI_ROW(5), (err & ERR_HS_FLT_CAP) ? "CAP" : "   ");
    draw_at(COL_F_2,      UI_ROW(5), (err & ERR_HS_FLT_SBC) ? "SBC" : "   ");
    draw_at(COL_F_3,      UI_ROW(5), (err & ERR_OVERHEAT)   ? "OVH" : "   ");
}

/* ============================================================
 * DisplayContent_Update — called by DisplayScheduler_Task every 500 ms
 * ============================================================ */
void DisplayContent_Update(void)
{
    static uint8_t tick = 0;

    tick++;
    if (tick >= (PAGE_DWELL * (uint8_t)PAGE_COUNT)) tick = 0;

    DisplayPage_t page = (DisplayPage_t)(tick / PAGE_DWELL);

    SSD1306_Fill(SSD1306_COLOR_BLACK);

    switch (page) {
        case PAGE_OVERVIEW:    Draw_Page_Overview();    break;
        case PAGE_RAIL_STATUS: Draw_Page_RailStatus();  break;
        case PAGE_FAULT_DETAIL:Draw_Page_FaultDetail(); break;
        default:               Draw_Page_Overview();    break;
    }

    /* Small page indicator: draw 3 tiny rectangles at bottom-right
     * current page rectangle is filled, others are outline.         */
    for (uint8_t p = 0; p < (uint8_t)PAGE_COUNT; p++) {
        uint16_t px = 128 - ((uint16_t)(PAGE_COUNT - p) * 6);
        uint16_t py = SSD1306_HEIGHT - 3;
        if (p == (uint8_t)page) {
            SSD1306_DrawFilledRectangle(px, py, 4, 2, SSD1306_COLOR_WHITE);
        } else {
            SSD1306_DrawRectangle(px, py, 4, 2, SSD1306_COLOR_WHITE);
        }
    }

    SSD1306_UpdateScreen();
}

/* ============================================================
 * UI_Display_Init
 * ============================================================ */
void UI_Display_Init(void)
{
    memset(&display_data, 0, sizeof(display_data));
    display_data.state = SYS_INIT;

    /* Show startup screen immediately */
    SSD1306_Fill(SSD1306_COLOR_BLACK);
    SSD1306_GotoXY(10, UI_ROW(2));
    SSD1306_Puts("POWERSTAGE INIT", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen();
}

/* ============================================================
 * UI_Display_Update — force immediate redraw
 * ============================================================ */
void UI_Display_Update(void)
{
    DisplayContent_Update();
}

/* ============================================================
 * UI_Display_SetState
 * ============================================================ */
void UI_Display_SetState(SystemState_t state)
{
    display_data.state = state;
}

/* ============================================================
 * UI_Display_SetErrorCode
 * ============================================================ */
void UI_Display_SetErrorCode(uint16_t error_code)
{
    display_data.error_code = error_code;
}

/* ============================================================
 * UI_Display_UpdateFromModules
 * Copy live data from io_module + fan_ctrl + can_operation
 * into display_data and rebuild error_code.
 * Call once per main-loop cycle BEFORE DisplayScheduler_Task().
 * ============================================================ */
void UI_Display_UpdateFromModules(SystemMeasurement_t *ms, FanCTRL_t *fan)
{
    /* Voltages */
    display_data.v24_mV  = ms->voltage_mV.V24;
    display_data.vcap_mV = ms->voltage_mV.VCAP;
    display_data.v12_mV  = ms->voltage_mV.V12;
    display_data.temp_C  = ms->NTCTemperature_C;

    /* Battery SOC (already computed by Run_Measurements via Battery_EstimateSOC_pct) */
    display_data.battery_soc_pct = ms->battery_soc_pct;

    /* Currents */
    display_data.i_bat_mA   = ms->current_mA._currbat;
    display_data.i_cap_mA   = ms->current_mA._currcap;
    display_data.i_sbc_mA   = ms->current_mA._currsbc;
    display_data.i_drive_mA = ms->current_mA._currdrive;
    display_data.i_aux_mA   = ms->current_mA._curraux;
    display_data.i_led_mA   = ms->current_mA._currled;

    /* Fan */
    display_data.fan_mode = (uint8_t)fan->Mode;
    display_data.fan_duty = fan->dutycycle_pct;

    /* HS rail status */
    display_data.hs_pgood_mask   = 0;
    display_data.hs_fault_mask   = oc_status.oc_fault_mask;
    display_data.hs_oc_warn_mask = oc_status.oc_warn_mask;
    display_data.uv_fault_mask   = uv_status.uv_fault_mask;

    for (uint8_t i = 0; i < RAIL_COUNT; i++) {
        if (HS_PGood(&hotswap[i]))
            display_data.hs_pgood_mask |= (1u << i);
    }

    /* ---- Auto-build error_code from all active faults ---- */
    uint16_t err = ERR_NONE;

    /* UV */
    if (uv_status.uv_fault_mask & UV_FAULT_V24)  err |= ERR_UV_V24;
    if (uv_status.uv_fault_mask & UV_FAULT_VCAP) err |= ERR_UV_VCAP;
    if (uv_status.uv_fault_mask & UV_FAULT_V12)  err |= ERR_UV_V12;

    /* Software OC warning */
    if (oc_status.oc_warn_mask & (1u << RAIL_AUX))   err |= ERR_OC_AUX;
    if (oc_status.oc_warn_mask & (1u << RAIL_LED))   err |= ERR_OC_LED;
    if (oc_status.oc_warn_mask & (1u << RAIL_DRIVE)) err |= ERR_OC_DRIVE;
    if (oc_status.oc_warn_mask & (1u << RAIL_CAP))   err |= ERR_OC_CAP;
    if (oc_status.oc_warn_mask & (1u << RAIL_SBC))   err |= ERR_OC_SBC;

    /* Hardware FLT (TPS2493) */
    if (oc_status.oc_fault_mask & (1u << RAIL_AUX))   err |= ERR_HS_FLT_AUX;
    if (oc_status.oc_fault_mask & (1u << RAIL_LED))   err |= ERR_HS_FLT_LED;
    if (oc_status.oc_fault_mask & (1u << RAIL_DRIVE)) err |= ERR_HS_FLT_DRV;
    if (oc_status.oc_fault_mask & (1u << RAIL_CAP))   err |= ERR_HS_FLT_CAP;
    if (oc_status.oc_fault_mask & (1u << RAIL_SBC))   err |= ERR_HS_FLT_SBC;

    /* Thermal */
    if (ms->NTCTemperature_C >= UI_OVERHEAT_TEMP_C)   err |= ERR_OVERHEAT;

    display_data.error_code = err;

    /* Update system state */
    if (err != ERR_NONE) {
        if (display_data.state == SYS_RUNNING)
            display_data.state = SYS_FAULT;
    } else {
        if (display_data.state == SYS_FAULT)
            display_data.state = SYS_RUNNING;
    }
}
