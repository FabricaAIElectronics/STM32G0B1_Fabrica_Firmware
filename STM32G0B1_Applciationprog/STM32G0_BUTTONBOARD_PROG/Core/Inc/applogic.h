/**
  ******************************************************************************
  * @file    applogic.h
  * @brief   Top-level state machine for the calibration knob / button board.
  ******************************************************************************
  */

#ifndef INC_APPLOGIC_H_
#define INC_APPLOGIC_H_

#include "main.h"
#include "eeprom_driver.h"
#include "can_operation.h"
#include "inputs.h"
#include "leds.h"
#include <stdbool.h>

/* Reported in the high nibble of KNOBSTATE byte 6 and in DEVSTATUS byte 0.
 * The numbering follows the STM32F303 knob build so existing host-side
 * decoders keep working. */
typedef enum {
    STATE_INIT        = 0x00,
    STATE_LOAD_CONFIG = 0x01,
    STATE_RUNNING     = 0x02,
    STATE_ERROR       = 0x03
} AppState;

/* DEVSTATUS byte 1 — a bitmask, so several conditions can be reported at
 * once. Only ERR_CAN is fatal; the others are advisory and the board keeps
 * running, because a bad rotary contact is no reason to stop reporting the
 * buttons. */
typedef enum {
    ERR_NONE           = 0x00,
    ERR_ROTARY_INVALID = 0x01,  /* RS1 decode invalid beyond the dwell      */
    ERR_EEPROM         = 0x02,  /* config unreadable, defaults in use       */
    ERR_CAN            = 0x04   /* FDCAN would not start                    */
} ErrorCode;

/* --------------------------------------------------------------------------
 * Scheduling
 * --------------------------------------------------------------------------
 * Inputs are polled every loop pass; the debouncers are tick-based so the
 * poll rate does not change their behaviour.
 *
 * KNOBSTATE and BUTTONS go out every 100 ms — fast enough that a deliberate
 * button press is never missed by a host polling at video rates.
 *
 * The three slow frames rotate one per TICK_ROTATE_MS rather than all going
 * out together, which keeps the board from putting three back-to-back frames
 * on the bus every half second. Each still reaches the host about twice a
 * second.
 */
#define TICK_FAST_MS        100U
#define TICK_ROTATE_MS      167U
#define TICK_INIT_MS        100U
#define TICK_ERROR_RETRY_MS 2000U

typedef struct {
    AppState   state;
    uint8_t    errorMask;

    Config     config;
    InputState inputs;

    uint32_t   lastFastTick;
    uint32_t   lastRotateTick;
    uint32_t   stateEntryTick;
    uint8_t    rotatePhase;
} AppStateMachine;

/* can_ok is CAN_Init()'s result. false starts the machine in STATE_ERROR
 * with ERR_CAN set: no telemetry is possible without CAN, so the only
 * useful behaviour is a visibly-different heartbeat and a reset-retry. */
void AppLogic_Init(AppStateMachine *sm, bool can_ok);
void AppLogic_Run(AppStateMachine *sm);

#endif /* INC_APPLOGIC_H_ */
