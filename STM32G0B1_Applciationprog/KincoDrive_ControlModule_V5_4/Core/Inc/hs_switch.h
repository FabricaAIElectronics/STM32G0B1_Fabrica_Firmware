/**
 * @file    hs_switch.h
 * @brief   High-side power switch enable/disable (digital outputs only).
 *
 * @details Controls 3 high-side power channels (Drive, Extruder, Scrubbing)
 *          and the 12 V buck converter. Each function only toggles its EN
 *          GPIO; there is NO automatic shutdown on PG/FT pin state — that
 *          is the application's responsibility (see power_monitor.c for
 *          OC tripping).
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef HS_SWITCH_H
#define HS_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#define HS_SUCCESS            0
#define HS_ERR_INVALID_PARAM (-1)

typedef enum {
    HS_MODULE_DRIVE     = 0,
    HS_MODULE_EXTRUDER  = 1,
    HS_MODULE_SCRUBBING = 2,
    HS_MODULE_COUNT     = 3
} HS_Module_t;

/* ────────── HS channel control ────────── */

/** Drive EN GPIO HIGH for the specified channel. */
int  HS_Enable(HS_Module_t module);

/** Drive EN GPIO LOW for the specified channel. */
int  HS_Disable(HS_Module_t module);

/** Read back the EN GPIO state. */
bool HS_IsEnabled(HS_Module_t module);

/** Read the TPS2493 PGOOD pin (active-low input → returns true when rail is good). */
bool HS_PowerGood(HS_Module_t module);

/** Read the TPS2493 FAULT pin (active-low input → returns true when fault asserted). */
bool HS_FaultPin(HS_Module_t module);

/* ────────── 12 V buck converter ────────── */

/** Enable 12 V buck (no preconditions). */
void Buck12V_Enable(void);

/** Disable 12 V buck. */
void Buck12V_Disable(void);

/** Read back the buck enable GPIO. */
bool Buck12V_IsEnabled(void);

#endif /* HS_SWITCH_H */
