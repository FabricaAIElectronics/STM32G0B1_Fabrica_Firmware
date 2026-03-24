/**
 * @file    Core_Systems.h
 * @brief   Top-level system state machine that runs periodic tasks.
 *
 * @details Calls protection checks, ESTOP state machine, and error
 *          state enforcement every main-loop iteration.  Routes to
 *          NORMAL, RECOVERY, or ERROR behaviour based on the current
 *          system state from the error manager.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef CORE_SYSTEMS_H
#define CORE_SYSTEMS_H

/**
 * @brief  Run periodic safety tasks (protection, ESTOP, error enforcement).
 *         Called internally by CoreSystem_TOP(); exposed for testing.
 */
void CoreSystem_PeriodicTasks(void);

/**
 * @brief  Top-level system tick — call once per main-loop iteration.
 *         Routes to the appropriate handler based on system state.
 */
void CoreSystem_TOP(void);

#endif /* CORE_SYSTEMS_H */
