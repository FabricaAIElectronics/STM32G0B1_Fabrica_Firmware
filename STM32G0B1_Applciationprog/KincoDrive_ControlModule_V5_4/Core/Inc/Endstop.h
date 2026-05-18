/**
 * @file    Endstop.h
 * @brief   Endstop switch reading and CAN telemetry packing.
 *
 * @details Reads 5 endstop switches (NC+NO or NO-only) and packs their
 *          triggered/fault state into 2 CAN bytes alongside the ESTOP state.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef ENDSTOP_H
#define ENDSTOP_H

#include <stdint.h>
#include <stddef.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Return codes
 * ════════════════════════════════════════════════════════════════════════════ */

#define ENDSTOP_OK          0
#define ENDSTOP_TRIGGERED   1
#define ENDSTOP_FAULT       2
#define PARAM_ERROR        (-1)

/* ════════════════════════════════════════════════════════════════════════════
 *  Endstop identifiers
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ENDSTOP_EXTRUDER_HEIGHT_TOP    = 0,
    ENDSTOP_EXTRUDER_HEIGHT_BOTTOM = 1,
    ENDSTOP_EXTRUDER_MOBILE_TOP    = 2,
    ENDSTOP_EXTRUDER_MOBILE_BOTTOM = 3,
    ENDSTOP_SCRUBBING_FRONT_TOP    = 4,
    NUM_ENDSTOPS                   = 5
} Endstop_Module_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

/** Initialize endstop state tracking. */
void Endstop_Init(void);

/**
 * @brief  Check the state of the specified endstop.
 * @retval ENDSTOP_OK, ENDSTOP_TRIGGERED, ENDSTOP_FAULT, or PARAM_ERROR.
 */
int Endstop_Check(Endstop_Module_t endstop);

/**
 * @brief  Pack endstop + ESTOP states into 2 CAN bytes.
 *
 *         Byte 0 (triggered):  bit N = 1 if endstop N is triggered
 *         Byte 1 (fault):      bit N = 1 if endstop N has a wiring fault
 *
 *         Bit 0: EXTRUDER_HEIGHT_TOP
 *         Bit 1: EXTRUDER_HEIGHT_BOTTOM
 *         Bit 2: EXTRUDER_MOBILE_TOP
 *         Bit 3: EXTRUDER_MOBILE_BOTTOM
 *         Bit 4: SCRUBBING_FRONT_TOP
 *         Bit 5: ESTOP
 *         Bit 6–7: reserved (0)
 *
 * @param  out1      Pointer to triggered byte.
 * @param  out2      Pointer to fault byte.
 * @param  out_size  Total output size available (must be >= 2).
 * @retval Bytes written (2) or 0 on error.
 */
size_t CAN_Packer_Endstop_and_ESTOP_2Byte(uint8_t *out1, uint8_t *out2, size_t out_size);

#endif /* ENDSTOP_H */
