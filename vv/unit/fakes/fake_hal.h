/* Minimal fake HAL surface for host-side tests. */
#ifndef VV_FAKE_HAL_H
#define VV_FAKE_HAL_H

#include <stdint.h>

extern uint32_t fake_tick_ms;
static inline uint32_t HAL_GetTick(void) { return fake_tick_ms; }

#endif /* VV_FAKE_HAL_H */
