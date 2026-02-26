#ifndef ENDSTOP_H_
#define ENDSTOP_H_

#include <stdint.h>
#include "main.h"
#include <stddef.h>


#define PARAM_ERROR       -1
#define ENDSTOP_OK          0
#define ENDSTOP_TRIGGERED   1
#define ENDSTOP_FAULT       2

typedef enum {
    ENDSTOP_EXTRUDER_HEIGHT_TOP = 0,
    ENDSTOP_EXTRUDER_HEIGHT_BOTTOM,
    ENDSTOP_EXTRUDER_MOBILE_TOP,
    ENDSTOP_EXTRUDER_MOBILE_BOTTOM,
    ENDSTOP_SCRUBBING_FRONT_TOP,
    NUM_ENDSTOPS
} Endstop_Module_t;



void Endstop_Init(void);

/**
* @brief Check the state of the specified endstop.
* @param endstop: The endstop module to check.
* @return ENDSTOP_OK if not triggered, ENDSTOP_TRIGGERED if triggered, ENDSTOP_FAULT on error.

*/
int Endstop_Check(Endstop_Module_t endstop);

/**
  * @brief  Pack endstop + ESTOP states into 2 CAN bytes.
  *
  * Byte 0 — Triggered state (1 bit per channel):
  *   Bit 0: ENDSTOP_EXTRUDER_HEIGHT_TOP
  *   Bit 1: ENDSTOP_EXTRUDER_HEIGHT_BOTTOM
  *   Bit 2: ENDSTOP_EXTRUDER_MOBILE_TOP
  *   Bit 3: ENDSTOP_EXTRUDER_MOBILE_BOTTOM
  *   Bit 4: ENDSTOP_SCRUBBING_FRONT_TOP
  *   Bit 5: ESTOP
  *   Bit 6–7: reserved (0)
  *
  *   0 = not triggered (OK or fault),  1 = triggered
  *
  * Byte 1 — Fault state (1 bit per channel, same bit positions):
  *   0 = no fault,  1 = fault
  *
  * @param  out1     Pointer to byte 0 (triggered).
  * @param  out2     Pointer to byte 1 (fault).
  * @param  out_size Total output size available (must be >= 2).
  * @retval Number of bytes written (2) or 0 on error.
  */
size_t CAN_Packer_Endstop_and_ESTOP_2Byte(uint8_t *out1, uint8_t *out2, size_t out_size);




#endif /* ENDSTOP_H_ */