/**
 * @file    ESTOP.h
 * @brief   Emergency stop switch reading and debounced state machine.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef ESTOP_H
#define ESTOP_H

#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Return codes
 * ════════════════════════════════════════════════════════════════════════════ */

#define ESTOP_OK          0
#define ESTOP_TRIGGERED   1
#define ESTOP_FAULT      (-1)

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

/** Initialize ESTOP state and LED. */
void ESTOP_Init(void);

/** Read raw ESTOP switch state (NC+NO cross-check). */
int ESTOP_Check(void);

/** Return the debounced ESTOP flag (0=OK, 1=triggered). */
int ESTOP_State(void);

/**
 * @brief  Debounced ESTOP state machine. Call periodically from the main loop.
 *         Updates the ESTOP LED output and internal flag.
 */
void ESTOP_State_Machine(void);

#endif /* ESTOP_H */
