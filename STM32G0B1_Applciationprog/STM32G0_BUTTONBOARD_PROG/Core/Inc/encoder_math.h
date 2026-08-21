/**
  ******************************************************************************
  * @file    encoder_math.h
  * @brief   Quadrature count -> detent number. Pure arithmetic, no HAL, so
  *          the vv host harness can compile and assert it directly.
  ******************************************************************************
  *
  * PEC11L: one full quadrature cycle (4 counts in x4 mode) per detent, and
  * the knob RESTS at a multiple of 4 counts - the counter is zeroed while
  * sitting in a detent, and Inputs_SetEncoder() seeds it the same way.
  *
  * A plain floor(accum / 4) puts every report boundary at exactly 4k, i.e.
  * exactly where the knob rests. One count of contact jitter at rest then
  * flips the report between k and k-1, and approaching a detent from the
  * two directions settles on different values (CW flips at the detent, CCW
  * flips three counts early). Bench-confirmed on the V5.5 Nucleo rig.
  *
  * Adding half a detent before flooring recentres the bins:
  *
  *     floor((accum + 2) / 4)     ->   [4k-2, 4k+1]  reports k
  *
  * so the rest position 4k sits in the MIDDLE of its bin with two counts of
  * margin on either side before the report changes. That is hysteresis by
  * geometry, symmetric in both directions, and it costs nothing at runtime.
  */

#ifndef INC_ENCODER_MATH_H_
#define INC_ENCODER_MATH_H_

#include <stdint.h>

#define ENCODER_COUNTS_PER_DETENT   4

/* Floor division of a signed count by ENCODER_COUNTS_PER_DETENT with a
 * half-detent offset. Written out (rather than relying on C's truncation
 * toward zero) so negative accumulators bin exactly like positive ones. */
static inline int16_t Encoder_CountsToDetents(int32_t accum)
{
    const int32_t shifted = accum + (ENCODER_COUNTS_PER_DETENT / 2);
    int32_t q = shifted / ENCODER_COUNTS_PER_DETENT;
    if ((shifted % ENCODER_COUNTS_PER_DETENT) < 0) {
        q -= 1;                                    /* floor, not truncate */
    }
    return (int16_t)q;
}

/* Inverse used by Inputs_SetEncoder(): the accumulator value that puts a
 * given detent number at its rest position (bin centre). */
static inline int32_t Encoder_DetentsToCounts(int16_t detents)
{
    return (int32_t)detents * ENCODER_COUNTS_PER_DETENT;
}

#endif /* INC_ENCODER_MATH_H_ */
