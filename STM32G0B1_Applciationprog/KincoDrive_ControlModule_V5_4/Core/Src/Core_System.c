/**
 * @file    Core_System.c
 * @brief   Top-level system state machine.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#include "Core_Systems.h"
#include "Power_Electronic.h"
#include "ESTOP.h"
#include "eeprom_driver.h"
#include "error_manager.h"

/* ════════════════════════════════════════════════════════════════════════════
 *  Periodic safety tasks (run in every state)
 * ════════════════════════════════════════════════════════════════════════════ */

void CoreSystem_PeriodicTasks(void)
{
    Shutdown_Protection();
    ESTOP_State_Machine();
    Error_Manager_EnforceState();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  State-specific handlers
 * ════════════════════════════════════════════════════════════════════════════ */

static void CoreSystem_Normal(void)
{
    CoreSystem_PeriodicTasks();
}

static void CoreSystem_Recovery(void)
{
    Error_Manager_AttemptRecovery();
}

static void CoreSystem_Error(void)
{
    CoreSystem_PeriodicTasks();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Top-level dispatch — call once per main-loop iteration
 * ════════════════════════════════════════════════════════════════════════════ */

void CoreSystem_TOP(void)
{
    SystemState_t state = Error_Manager_GetState();

    switch (state) {
    case STATE_ERROR:
        CoreSystem_Error();
        break;
    case STATE_RECOVERY:
        CoreSystem_Recovery();
        break;
    case STATE_WARNING:
        /* Warnings don't block normal operation */
        /* fall through */
    case STATE_NORMAL:
    default:
        CoreSystem_Normal();
        break;
    }
}
