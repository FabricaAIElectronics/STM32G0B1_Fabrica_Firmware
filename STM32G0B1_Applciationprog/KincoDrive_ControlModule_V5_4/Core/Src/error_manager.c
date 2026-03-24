/**
 * @file    error_manager.c
 * @brief   Centralized error logging, system state machine, and CAN error dump.
 *
 * @author  jordan
 * @date    2026-02-24
 */

#include "error_manager.h"
#include "Power_Electronic.h"
#include "CAN_Handler.h"
#include "fdcan.h"
#include "eeprom_driver.h"
#include "stm32g0xx_hal.h"
#include <string.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Internal state
 * ════════════════════════════════════════════════════════════════════════════ */

static SystemState_t system_state = STATE_NORMAL;

/* Ring-buffer error log */
static ErrorEntry_t error_log[ERROR_LOG_MAX];
static uint8_t      error_count = 0;
static uint8_t      error_head  = 0;

/* Per-module lockout tracking */
static bool critical_all_modules    = false;
static bool critical_module[3]      = {false, false, false};

/* Recovery state */
static uint8_t  recovery_ok_count   = 0;
static uint32_t recovery_start_ms   = 0;

/* ════════════════════════════════════════════════════════════════════════════
 *  Initialization
 * ════════════════════════════════════════════════════════════════════════════ */

void Error_Manager_Init(void)
{
    system_state         = STATE_NORMAL;
    error_count          = 0;
    error_head           = 0;
    critical_all_modules = false;
    recovery_ok_count    = 0;
    recovery_start_ms    = 0;

    memset(critical_module, 0, sizeof(critical_module));
    memset(error_log, 0, sizeof(error_log));
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Error logging
 * ════════════════════════════════════════════════════════════════════════════ */

void Error_Manager_Log(ErrorSource_t source, ErrorSeverity_t severity, uint16_t detail)
{
    /* Write into ring buffer (oldest overwritten when full) */
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
        case ERR_SRC_OVERCURRENT_DRIVE:  critical_module[0] = true; break;
        case ERR_SRC_OVERCURRENT_EXT:    critical_module[1] = true; break;
        case ERR_SRC_OVERCURRENT_SCRUB:  critical_module[2] = true; break;
        case ERR_SRC_OVERCURRENT_BUS:
        case ERR_SRC_OVERVOLTAGE_HARD:
        case ERR_SRC_UNDERVOLTAGE:
        default:
            critical_all_modules = true;
            break;
        }
    } else if (severity == ERR_SEV_WARNING && system_state == STATE_NORMAL) {
        system_state = STATE_WARNING;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  State queries
 * ════════════════════════════════════════════════════════════════════════════ */

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

/* ════════════════════════════════════════════════════════════════════════════
 *  State enforcement — keep locked-out modules disabled
 * ════════════════════════════════════════════════════════════════════════════ */

void Error_Manager_EnforceState(void)
{
    if (system_state != STATE_ERROR && system_state != STATE_RECOVERY) {
        return;
    }

    if (critical_all_modules) {
        Disable_HighSide_Power_All(500);
    } else {
        static const HighSide_Module_t modules[3] = {
            HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING
        };
        for (int i = 0; i < 3; ++i) {
            if (critical_module[i]) {
                Disable_HighSide_Power_Module(modules[i], 500);
            }
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN handler: dump all logged errors (0x703)
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_Dump_Errors(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id; (void)params; (void)len;

    if (error_count == 0) {
        uint8_t term[8];
        memset(term, 0xFF, sizeof(term));
        FDCAN_SendFrame(CAN_ID_ERROR_DUMP, term, 8);
        return;
    }

    /* Start from oldest entry */
    uint8_t start = (error_count < ERROR_LOG_MAX) ? 0 : error_head;

    for (uint8_t i = 0; i < error_count; ++i) {
        uint8_t idx = (start + i) % ERROR_LOG_MAX;
        ErrorEntry_t *e = &error_log[idx];

        uint8_t payload[8] = {0};
        payload[0] = i;
        payload[1] = (uint8_t)e->source;
        payload[2] = (uint8_t)e->severity;
        payload[3] = (uint8_t)(e->detail & 0xFF);
        payload[4] = (uint8_t)((e->detail >> 8) & 0xFF);
        payload[5] = (uint8_t)(e->timestamp_ms & 0xFF);
        payload[6] = (uint8_t)((e->timestamp_ms >> 8) & 0xFF);
        payload[7] = (uint8_t)((e->timestamp_ms >> 16) & 0xFF);

        FDCAN_SendFrame(CAN_ID_ERROR_DUMP, payload, 8);
        HAL_Delay(2);   /* avoid flooding TX mailbox */
    }

    /* Terminator frame */
    uint8_t term[8];
    memset(term, 0xFF, sizeof(term));
    FDCAN_SendFrame(CAN_ID_ERROR_DUMP, term, 8);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  CAN handler: reset from ERROR → RECOVERY (0x704)
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_Reset_Error(uint32_t id, uint8_t *params, uint8_t len)
{
    (void)id;

    /* Safety key: payload[0] must be 0xAA */
    if (len < 1 || params == NULL || params[0] != 0xAA) {
        send_nack(CAN_ID_ERROR_RESET_ACK, 0x00);
        return;
    }

    if (system_state != STATE_ERROR) {
        send_nack(CAN_ID_ERROR_RESET_ACK, 0x01);
        return;
    }

    /* Clear error log */
    error_count = 0;
    error_head  = 0;
    memset(error_log, 0, sizeof(error_log));

    /* Enter recovery (lockout flags stay active until AttemptRecovery clears them) */
    system_state      = STATE_RECOVERY;
    recovery_ok_count = 0;
    recovery_start_ms = HAL_GetTick();

    send_ack(CAN_ID_ERROR_RESET_ACK, 0xBB);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Recovery validation state machine
 * ════════════════════════════════════════════════════════════════════════════ */

void Error_Manager_AttemptRecovery(void)
{
    if (system_state != STATE_RECOVERY) return;

    /* Timeout check */
    if ((HAL_GetTick() - recovery_start_ms) >= RECOVERY_TIMEOUT_MS) {
        system_state = STATE_ERROR;
        Error_Manager_Log(ERR_SRC_NONE, ERR_SEV_CRITICAL, 0xFFFF);
        send_nack(CAN_ID_ERROR_RESET_ACK, 0xEE);
        return;
    }

    /* Get config thresholds */
    const Config *cfgp = EEPROM_GetCachedConfig();
    Config cfg_local;
    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &cfg_local);
        if (checkcfg(&cfg_local)) {
            cfgp = &cfg_local;
        } else {
            return;     /* no valid config — retry next loop */
        }
    }

    bool all_ok = true;

    /* 24V bus voltage check */
    uint32_t bus_1dp = 0;
    Read_24V_Voltage_1DP(&bus_1dp);
    uint32_t bus_mV = bus_1dp * 100U;

    if (bus_mV >= (uint32_t)cfgp->hard_over_voltage) {
        recovery_ok_count = 0;
        system_state = STATE_ERROR;
        Error_Manager_Log(ERR_SRC_OVERVOLTAGE_HARD, ERR_SEV_CRITICAL, (uint16_t)bus_mV);
        send_nack(CAN_ID_ERROR_RESET_ACK, 0xCC);
        return;
    }

    /* Per-module overcurrent check */
    static const HighSide_Module_t modules[3] = {
        HS_MODULE_DRIVE, HS_MODULE_EXTRUDER, HS_MODULE_SCRUBBING
    };
    static const ErrorSource_t oc_src[3] = {
        ERR_SRC_OVERCURRENT_DRIVE, ERR_SRC_OVERCURRENT_EXT, ERR_SRC_OVERCURRENT_SCRUB
    };

    for (int i = 0; i < 3; ++i) {
        uint32_t current_mA = 0;
        if (Read_HighSide_Module_Current_mA(modules[i], &current_mA) != PE_SUCCESS) {
            continue;
        }
        if (current_mA >= (uint32_t)cfgp->over_current) {
            all_ok = false;
            recovery_ok_count = 0;
            system_state = STATE_ERROR;
            Error_Manager_Log(oc_src[i], ERR_SEV_CRITICAL, (uint16_t)current_mA);
            send_nack(CAN_ID_ERROR_RESET_ACK, 0xEE);
            return;
        }
    }

    /* Evaluate stability */
    if (all_ok) {
        recovery_ok_count++;
    } else {
        recovery_ok_count = 0;
    }

    if (recovery_ok_count >= RECOVERY_STABLE_LOOPS) {
        /* Recovery succeeded */
        critical_all_modules = false;
        memset(critical_module, 0, sizeof(critical_module));
        recovery_ok_count = 0;
        system_state = STATE_NORMAL;

        Shutdown_Protection_ResetState();
        send_ack(CAN_ID_ERROR_RESET_ACK, 0xAA);
    }
}
