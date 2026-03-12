/**
  * @file    error_manager.h
  * @brief   Centralized error logging, system state management, and CAN error dump.
  *
  * @details Provides a RAM-resident error log (ring buffer) and a global system
  *          operating state machine:
  *
  *          STATE_NORMAL  — all CAN commands functional, power modules controllable.
  *          STATE_WARNING — non-critical error logged (e.g. endstop fault), system
  *                          continues to operate normally, error is recorded for
  *                          later retrieval.
  *          STATE_ERROR   — critical fault (overcurrent, overvoltage). Affected
  *                          module(s) are shut down. CAN commands to enable HS
  *                          power are blocked. CAN telemetry continues to broadcast.
  *                          Only a reset command (0x702) can move to STATE_RECOVERY.
  *          STATE_RECOVERY— transient validation state entered after a reset
  *                          command. Power remains blocked while the system
  *                          checks voltage and current readings against safe
  *                          thresholds for RECOVERY_STABLE_LOOPS consecutive
  *                          calls. If all checks pass, transitions to
  *                          STATE_NORMAL. If any check fails, returns to
  *                          STATE_ERROR and re-logs the fault.
  *
  *          Error retrieval:
  *            CAN 0x701 (no payload) → MCU replies with one CAN frame per logged
  *            error on CAN ID 0x701, then a terminator frame.
  *
  *          Error clear / reset:
  *            CAN 0x702 payload[0]=0xAA → clears error log, enters STATE_RECOVERY.
  *            System validates voltage/current for RECOVERY_STABLE_LOOPS cycles
  *            before returning to STATE_NORMAL.  If validation fails, returns
  *            to STATE_ERROR.
  *
  *  Created on: 24 Feb 2026
  *      Author: jordan
  */

#ifndef INC_ERROR_MANAGER_H_
#define INC_ERROR_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

/* ── System operating states ── */
typedef enum {
    STATE_NORMAL   = 0,    /* all systems operational */
    STATE_WARNING  = 1,    /* non-critical fault logged, system continues */
    STATE_ERROR    = 2,    /* critical fault, power blocked until reset */
    STATE_RECOVERY = 3     /* reset requested, validating voltage/current before allowing NORMAL */
} SystemState_t;

/* ── Error severity ── */
typedef enum {
    ERR_SEV_WARNING  = 0, /* logged only, does not block operation */
    ERR_SEV_CRITICAL = 1  /* logged + enters ERROR state + shuts down power */
} ErrorSeverity_t;

/* ── Error source codes ── */
typedef enum {
    ERR_SRC_NONE              = 0x00,
    ERR_SRC_OVERCURRENT_DRIVE = 0x01,
    ERR_SRC_OVERCURRENT_EXT   = 0x02,
    ERR_SRC_OVERCURRENT_SCRUB = 0x03,
    ERR_SRC_OVERCURRENT_BUS   = 0x04,
    ERR_SRC_OVERVOLTAGE_SOFT  = 0x10,
    ERR_SRC_OVERVOLTAGE_HARD  = 0x11,
    ERR_SRC_UNDERVOLTAGE      = 0x12,
    ERR_SRC_ENDSTOP_FAULT     = 0x20,
    ERR_SRC_ESTOP_FAULT       = 0x21,
    ERR_SRC_ESTOP_TRIGGERED   = 0x22,
    ERR_SRC_THERMAL_FAULT     = 0x30,
    ERR_SRC_EEPROM_FAULT      = 0x40,
    ERR_SRC_CAN_BUS_FAULT     = 0x50,
    /* add more as needed */
} ErrorSource_t;

/* ── Single error log entry ── */
typedef struct {
    uint32_t       timestamp_ms;  /* HAL_GetTick() at time of error */
    ErrorSource_t  source;        /* what caused the error */
    ErrorSeverity_t severity;     /* warning or critical */
    uint16_t       detail;        /* extra info (e.g. measured mV, mA, module index) */
} ErrorEntry_t;

/* ── Max stored errors (ring buffer, oldest overwritten) ── */
#define ERROR_LOG_MAX  16

/* ── Recovery validation parameters ── */
#define RECOVERY_STABLE_LOOPS   5    /* consecutive OK checks required to go NORMAL */
#define RECOVERY_TIMEOUT_MS     5000 /* max time in RECOVERY before failing back to ERROR */

/* ── API ── */

/**
  * @brief  Initialise the error manager. Call once at startup.
  */
void Error_Manager_Init(void);

/**
  * @brief  Log an error. If severity is CRITICAL, system enters STATE_ERROR.
  * @param  source   Error source code.
  * @param  severity Warning or critical.
  * @param  detail   Additional context (e.g. measured value, module index).
  */
void Error_Manager_Log(ErrorSource_t source, ErrorSeverity_t severity, uint16_t detail);

/**
  * @brief  Get current system state.
  * @retval SystemState_t
  */
SystemState_t Error_Manager_GetState(void);

/**
  * @brief  Check whether HS power commands are allowed.
  *         Returns true only if state is STATE_NORMAL.
  * @retval true = power commands allowed, false = blocked
  */
bool Error_Manager_IsPowerAllowed(void);

/**
  * @brief  Enforce the error state: keep errored modules shut down.
  *         Call from the periodic task loop so that even if something
  *         tries to re-enable power it gets disabled again.
  */
void Error_Manager_EnforceState(void);

/**
  * @brief  CAN handler: dump all logged errors over CAN (0x701).
  *         Each error is sent as one 8-byte frame. A terminator frame
  *         with all-zeros is sent last.
  */
void CAN_Handler_Dump_Errors(uint32_t id, uint8_t *params, uint8_t len);

/**
  * @brief  CAN handler: reset system from ERROR state back to NORMAL.
  *         Payload[0] must be 0xAA as a safety key.
  *         Clears the error log and returns to STATE_NORMAL.
  */
void CAN_Handler_Reset_Error(uint32_t id, uint8_t *params, uint8_t len);

/**
  * @brief  Get the number of errors currently stored.
  * @retval uint8_t count (0 .. ERROR_LOG_MAX)
  */
uint8_t Error_Manager_GetCount(void);

/**
  * @brief  Run one iteration of the recovery validation state machine.
  *         Must be called periodically (e.g. from the main loop) while
  *         system_state == STATE_RECOVERY.
  *
  *         Reads 24V bus voltage, bus current, and per-module currents.
  *         If all are within safe thresholds for RECOVERY_STABLE_LOOPS
  *         consecutive calls, transitions to STATE_NORMAL.
  *         If any reading exceeds thresholds, or RECOVERY_TIMEOUT_MS
  *         elapses, transitions back to STATE_ERROR and re-logs the fault.
  *
  *         Harmless to call in any other state (returns immediately).
  */
void Error_Manager_AttemptRecovery(void);

#endif /* INC_ERROR_MANAGER_H_ */
