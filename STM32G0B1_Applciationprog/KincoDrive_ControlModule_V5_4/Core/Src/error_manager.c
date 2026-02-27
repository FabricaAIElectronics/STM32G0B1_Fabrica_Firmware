/**
  * @file    error_manager.c
  * @brief   Centralized error logging, system state management, and CAN error dump.
  *
  *  Created on: 24 Feb 2026
  *      Author: jordan
  */

#include "error_manager.h"
#include "Power_Electronic.h"
#include "CAN_Handler.h"
#include "fdcan.h"
#include "eeprom_driver.h"
#include "stm32g0xx_hal.h"
#include <string.h>

/* ── Internal state ── */
static SystemState_t system_state = STATE_NORMAL;

/* Ring-buffer error log */
static ErrorEntry_t  error_log[ERROR_LOG_MAX];
static uint8_t       error_count = 0;   /* total stored (capped at ERROR_LOG_MAX) */
static uint8_t       error_head  = 0;   /* next write index */

/* Track which subsystems caused the critical error so we can keep them shut down */
static bool critical_all_modules = false;           /* true = all HS modules locked off */
static bool critical_module[3]   = {false, false, false}; /* per-module lockout */

/* Recovery state tracking */
static uint8_t  recovery_ok_count   = 0;   /* consecutive passing checks */
static uint32_t recovery_start_ms   = 0;   /* HAL_GetTick() when recovery entered */

/* ──────────────────────────────────────────────────────────────────────────── */

void Error_Manager_Init(void)
{
    system_state = STATE_NORMAL;
    error_count  = 0;
    error_head   = 0;
    critical_all_modules = false;
    memset(critical_module, 0, sizeof(critical_module));
    memset(error_log, 0, sizeof(error_log));
    recovery_ok_count = 0;
    recovery_start_ms = 0;
}

/* ──────────────────────────────────────────────────────────────────────────── */

void Error_Manager_Log(ErrorSource_t source, ErrorSeverity_t severity, uint16_t detail)
{
    /* Write entry into ring buffer (oldest overwritten when full) */
    ErrorEntry_t *e = &error_log[error_head];
    e->timestamp_ms = HAL_GetTick();
    e->source       = source;
    e->severity     = severity;
    e->detail       = detail;

    error_head = (error_head + 1) % ERROR_LOG_MAX;
    if (error_count < ERROR_LOG_MAX) {
        error_count++;
    }

    /* State transitions */
    if (severity == ERR_SEV_CRITICAL) {
        system_state = STATE_ERROR;

        /* Mark which modules should stay locked off */
        switch (source) {
            case ERR_SRC_OVERCURRENT_DRIVE:
                critical_module[0] = true;
                break;
            case ERR_SRC_OVERCURRENT_EXT:
                critical_module[1] = true;
                break;
            case ERR_SRC_OVERCURRENT_SCRUB:
                critical_module[2] = true;
                break;
            case ERR_SRC_OVERCURRENT_BUS:
            case ERR_SRC_OVERVOLTAGE_HARD:
            case ERR_SRC_UNDERVOLTAGE:
                critical_all_modules = true;
                break;
            default:
                /* other critical sources: lock all as a safety fallback */
                critical_all_modules = true;
                break;
        }
    } else if (severity == ERR_SEV_WARNING && system_state == STATE_NORMAL) {
        system_state = STATE_WARNING;
    }
}

/* ──────────────────────────────────────────────────────────────────────────── */

SystemState_t Error_Manager_GetState(void)
{
    return system_state;
}

bool Error_Manager_IsPowerAllowed(void)
{
    return (system_state == STATE_NORMAL || system_state == STATE_WARNING);
}

uint8_t Error_Manager_GetCount(void)
{
    return error_count;
}

/* ──────────────────────────────────────────────────────────────────────────── */

/**
  * @brief  Enforce error state every loop: keep locked-out modules disabled.
  *         Also keeps modules disabled during STATE_RECOVERY validation.
  *         Harmless to call when in STATE_NORMAL (does nothing).
  */
void Error_Manager_EnforceState(void)
{
    if (system_state != STATE_ERROR && system_state != STATE_RECOVERY) {
        return;
    }

    if (critical_all_modules) {
        Disable_HighSide_Power_All(500);
    } else {
        HighSide_Module_t modules[] = { HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING };
        for (int i = 0; i < 3; ++i) {
            if (critical_module[i]) {
                Disable_HighSide_Power_Module(modules[i], 500);
            }
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────────── */

/**
  * @brief  CAN handler for 0x701: dump all logged errors over CAN.
  *
  *         Each error is sent as one 8-byte frame on CAN ID 0x701:
  *           Byte 0:    error index (0-based, oldest first)
  *           Byte 1:    source (ErrorSource_t)
  *           Byte 2:    severity (0=warning, 1=critical)
  *           Byte 3–4:  detail (uint16_t LE)
  *           Byte 5–7:  timestamp lower 3 bytes (ms, LE)
  *
  *         A terminator frame (all 0xFF) signals end-of-log.
  */
void CAN_Handler_Dump_Errors(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id; (void)params; (void)len;

    if (error_count == 0) {
        /* No errors: send empty terminator immediately */
        uint8_t term[8];
        memset(term, 0xFF, sizeof(term));
        FDCAN_SendFrame(0x701, term, 8);
        return;
    }

    /* Calculate oldest entry index */
    uint8_t start;
    if (error_count < ERROR_LOG_MAX) {
        start = 0;
    } else {
        start = error_head; /* head points to oldest when buffer is full */
    }

    for (uint8_t i = 0; i < error_count; ++i) {
        uint8_t idx = (start + i) % ERROR_LOG_MAX;
        ErrorEntry_t *e = &error_log[idx];

        uint8_t payload[8] = {0};
        payload[0] = i;                                   /* index */
        payload[1] = (uint8_t)e->source;                  /* source code */
        payload[2] = (uint8_t)e->severity;                /* severity */
        payload[3] = (uint8_t)(e->detail & 0xFF);         /* detail lo */
        payload[4] = (uint8_t)((e->detail >> 8) & 0xFF);  /* detail hi */
        payload[5] = (uint8_t)(e->timestamp_ms & 0xFF);         /* ts byte 0 */
        payload[6] = (uint8_t)((e->timestamp_ms >> 8) & 0xFF);  /* ts byte 1 */
        payload[7] = (uint8_t)((e->timestamp_ms >> 16) & 0xFF); /* ts byte 2 */

        FDCAN_SendFrame(0x701, payload, 8);

        /* Small delay to avoid flooding CAN TX mailbox */
        HAL_Delay(2);
    }

    /* Terminator frame */
    uint8_t term[8];
    memset(term, 0xFF, sizeof(term));
    FDCAN_SendFrame(0x701, term, 8);
}

/* ──────────────────────────────────────────────────────────────────────────── */

/**
  * @brief  CAN handler for 0x704: reset system from ERROR → RECOVERY.
  *         Payload[0] must be 0xAA as a safety key to prevent accidental resets.
  *         Clears the error log and enters STATE_RECOVERY, where voltage and
  *         current are validated before allowing STATE_NORMAL.
  */
void CAN_Handler_Reset_Error(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id;

    /* Safety key check */
    if (len < 1 || params == NULL || params[0] != 0xAA) {
        send_nack(0x702, 0x00);
        return;
    }

    /* Only allow reset from ERROR state */
    if (system_state != STATE_ERROR) {
        send_nack(0x702, 0x01);
        return;
    }

    /* Clear error log */
    error_count = 0;
    error_head  = 0;
    memset(error_log, 0, sizeof(error_log));

    /* Enter recovery — lockout flags are NOT cleared yet.
       They stay active so EnforceState keeps modules disabled
       until AttemptRecovery validates the readings and clears them. */
    system_state      = STATE_RECOVERY;
    recovery_ok_count = 0;
    recovery_start_ms = HAL_GetTick();

    /* ACK the reset (0xBB indicates "entered recovery, not yet normal") */
    send_ack(0x702, 0xBB);
}

/* ──────────────────────────────────────────────────────────────────────────── */

/**
  * @brief  Recovery state machine — called every main-loop iteration.
  *
  *         Reads 24V bus voltage/current and per-module currents and compares
  *         them against the EEPROM thresholds (same ones used by
  *         Shutdown_Protection).
  *
  *         • If all readings are within safe limits for RECOVERY_STABLE_LOOPS
  *           consecutive calls, clears lockout flags and transitions to
  *           STATE_NORMAL.  A CAN ACK (0x702, 0xAA) is sent to inform the host.
  *         • If any reading is out of range, the counter resets.
  *         • If RECOVERY_TIMEOUT_MS elapses before enough consecutive good
  *           readings are collected, the system falls back to STATE_ERROR and
  *           the offending measurement is re-logged as a new CRITICAL error.
  *
  *         Harmless to call in any other state — returns immediately.
  */
void Error_Manager_AttemptRecovery(void)
{
    if (system_state != STATE_RECOVERY) {
        return;
    }

    /* ── Timeout check ── */
    if ((HAL_GetTick() - recovery_start_ms) >= RECOVERY_TIMEOUT_MS) {
        /* Timed out: back to ERROR with a generic recovery-fail log */
        system_state = STATE_ERROR;
        Error_Manager_Log(ERR_SRC_NONE, ERR_SEV_CRITICAL, 0xFFFF);
        send_nack(0x702, 0xEE);  /* inform host: recovery failed (timeout) */
        return;
    }

    /* ── Get config thresholds ── */
    const Config *cfgp = EEPROM_GetCachedConfig();
    Config cfg_local;
    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &cfg_local);
        if (checkcfg(&cfg_local)) {
            cfgp = &cfg_local;
        } else {
            return; /* no valid config yet — try again next loop */
        }
    }

    bool all_ok = true;

    /* ── 24V bus voltage check ── */
    uint32_t bus_1dp = 0;
    Read_24V_Voltage_1DP(&bus_1dp);
    uint32_t bus_mV = bus_1dp * 100U;

    if (bus_mV >= (uint32_t)cfgp->hard_over_voltage) {
        /* Hard overvoltage still present */
        all_ok = false;
        recovery_ok_count = 0;
        system_state = STATE_ERROR;
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_HARD, ERR_SEV_CRITICAL, (uint16_t)bus_mV);
        send_nack(0x702, 0xCC);
        return;
    }

  

    /* ── Per-module overcurrent check ── */
    HighSide_Module_t modules[] = { HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING };
    ErrorSource_t     oc_src[]  = { ERR_SRC_OVERCURRENT_DRIVE, ERR_SRC_OVERCURRENT_EXT, ERR_SRC_OVERCURRENT_SCRUB };

    for (int i = 0; i < 3; ++i) {
        uint32_t current_mA = 0;
        if (Read_HighSide_Module_Current_mA(modules[i], &current_mA) != PE_SUCCESS) {
            continue; /* ADC read fail — skip, don't penalise */
        }
        if (current_mA >= (uint32_t)cfgp->over_current) {
            /* Overcurrent still present on this module */
            all_ok = false;
            recovery_ok_count = 0;
            system_state = STATE_ERROR;
            Error_Manager_Log(oc_src[i], ERR_SEV_CRITICAL, (uint16_t)current_mA);
            send_nack(0x702, 0xEE);
            return;
        }
    }

    /* ── Evaluate stability ── */
    if (all_ok) {
        recovery_ok_count++;
    } else {
        recovery_ok_count = 0;
    }

    if (recovery_ok_count >= RECOVERY_STABLE_LOOPS) {
        /* Recovery succeeded — clear lockout flags and go NORMAL */
        critical_all_modules = false;
        memset(critical_module, 0, sizeof(critical_module));
        recovery_ok_count = 0;
        system_state = STATE_NORMAL;

        /* Reset Shutdown_Protection edge-detection state so it can
           re-trigger immediately if a fault reappears after NORMAL. */
        Shutdown_Protection_ResetState();

        /* Inform the host that recovery completed successfully */
        send_ack(0x702, 0xAA);
    }
}
