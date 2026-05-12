/**
 * @file    power_monitor.h
 * @brief   Bus voltage / current reading and OC + UV protection.
 *
 * @details The only two protection trips on this board:
 *            - Overcurrent (OC) per HS channel  → trips and disables that channel
 *            - Undervoltage (UV) on 24 V or 12 V → reports flag, does NOT auto-disable
 *
 *          Thresholds are configurable at runtime (CAN command / EEPROM).
 *          Use Power_Monitor_RunProtection() periodically; check the return
 *          bitmask via Power_Monitor_GetErrorMask().
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>
#include "hs_switch.h"

/* ────────── Error bitmask ────────── */

#define ERR_NONE         0x00U
#define ERR_OC_DRIVE     (1U << 0)
#define ERR_OC_EXTRUDER  (1U << 1)
#define ERR_OC_SCRUBBING (1U << 2)
#define ERR_UV_24V       (1U << 3)
#define ERR_UV_12V       (1U << 4)

#define ERR_OC_MASK_ALL  (ERR_OC_DRIVE | ERR_OC_EXTRUDER | ERR_OC_SCRUBBING)
#define ERR_UV_MASK_ALL  (ERR_UV_24V | ERR_UV_12V)

/* ────────── Threshold defaults ────────── */

#define OC_THRESHOLD_DEFAULT_MA   5000U   /* 5 A per channel */
#define UV_24V_THRESHOLD_DEFAULT  20000U  /* 20.0 V */
#define UV_12V_THRESHOLD_DEFAULT  10000U  /* 10.0 V */

/* ────────── Threshold setters ────────── */

void PM_Set_OC_Threshold(HS_Module_t module, uint16_t threshold_mA);
void PM_Set_UV_24V_Threshold(uint16_t threshold_mV);
void PM_Set_UV_12V_Threshold(uint16_t threshold_mV);

uint16_t PM_Get_OC_Threshold(HS_Module_t module);
uint16_t PM_Get_UV_24V_Threshold(void);
uint16_t PM_Get_UV_12V_Threshold(void);

/* ────────── Live readings (always available, integer-only) ────────── */

uint32_t PM_Read_HS_Current_mA(HS_Module_t module);
uint32_t PM_Read_24V_Bus_Current_mA(void);
uint32_t PM_Read_24V_Voltage_mV(void);
uint32_t PM_Read_12V_Voltage_mV(void);

/* ────────── Protection runtime ────────── */

/**
 * @brief  Evaluate OC and UV against configured thresholds.
 *
 *         OC: any HS channel whose current exceeds its threshold AND is
 *             currently enabled is auto-disabled and its error bit is set.
 *             The bit STAYS set until the host re-enables that channel
 *             (via PM_Clear_OC_Error()).
 *
 *         UV: 24 V or 12 V below threshold sets the corresponding bit.
 *             Bit auto-clears when the bus recovers above threshold + hysteresis.
 *
 * @retval Current error mask (combination of ERR_OC_xxx / ERR_UV_xxx bits).
 */
uint8_t PM_RunProtection(void);

/** Get the cached error bitmask without re-evaluating. */
uint8_t PM_GetErrorMask(void);

/**
 * @brief  Clear the OC error bit for the given module. Called by the host
 *         after it has re-enabled the HS channel via the HS_POWER CAN cmd.
 *         If OC condition is still present, the bit will be re-set on the
 *         next PM_RunProtection() call.
 */
void PM_Clear_OC_Error(HS_Module_t module);

#endif /* POWER_MONITOR_H */
