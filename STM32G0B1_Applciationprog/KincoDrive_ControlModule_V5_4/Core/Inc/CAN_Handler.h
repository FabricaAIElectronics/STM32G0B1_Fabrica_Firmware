/**
 * @file    CAN_Handler.h
 * @brief   CAN protocol definitions and public API.
 *
 * @details All frames are 11-bit standard IDs in CANopen-coexistence-safe
 *          range 0x101..0x12F.  KincoDrive sub-block layout:
 *
 *            0x101  Bootloader RX  (XCP CONNECT + app reset trigger)
 *            0x102  Bootloader TX  (XCP responses)
 *            0x110  CMD_HS_POWER          (1B bitmask)
 *            0x111  CMD_FAN_PWM           (5B)
 *            0x112  CMD_EEPROM            (1B)
 *            0x113  CMD_OC_THRESHOLD      (6B)
 *            0x114  CMD_UV_THRESHOLD      (4B)
 *            0x120  BCAST_STATUS          (8B) — voltages, current, state, error
 *            0x121  BCAST_CURRENTS        (8B) — bus + per-HS currents
 *            0x122  BCAST_TEMPS           (6B) — 6× PTC
 *            0x123  BCAST_FANS            (5B) — fan tach %
 *            0x124  BCAST_GPIO            (8B) — full raw GPIO state
 *            0x125  BCAST_RAW_ADC         (6B) — packed nibbles
 *            0x126  BCAST_CONFIG_A        (8B) — HS state + 3× OC thresholds
 *            0x127  BCAST_CONFIG_B        (8B) — UV thresholds + fan defaults
 *
 * @author  jordan
 * @date    2026-05-06
 */

#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>
#include "applogic.h"

/* ────────── Bootloader / system ────────── */

/* Bootloader RX (host→target). XCP CONNECT here also triggers app reset.
 * Must match STM32G0B1_Bootloader/G0B1_KincoDrive_Boot/App/blt_conf.h. */
#define CAN_ID_BOOTLOADER           0x101U
#define CAN_ID_BOOTLOADER_TX        0x102U

/* ────────── Commands ────────── */

#define CAN_ID_CMD_HS_POWER         0x110U
#define CAN_ID_CMD_FAN_PWM          0x111U
#define CAN_ID_CMD_EEPROM           0x112U
#define CAN_ID_CMD_OC_THRESHOLD     0x113U
#define CAN_ID_CMD_UV_THRESHOLD     0x114U

/* ────────── Broadcasts ────────── */

#define CAN_ID_BCAST_STATUS         0x120U
#define CAN_ID_BCAST_CURRENTS       0x121U
#define CAN_ID_BCAST_TEMPS          0x122U
#define CAN_ID_BCAST_FANS           0x123U
#define CAN_ID_BCAST_GPIO           0x124U
#define CAN_ID_BCAST_RAW_ADC        0x125U
#define CAN_ID_BCAST_CONFIG_A       0x126U   /* hs_state + 3× OC threshold */
#define CAN_ID_BCAST_CONFIG_B       0x127U   /* UV thresholds + fan defaults */

/* ────────── Filter range ────────── */

#define CAN_ID_RANGE_LOW            0x101U
#define CAN_ID_RANGE_HIGH           0x12FU

/* ────────── Return codes ────────── */

#define CAN_SUCCESS     0
#define CAN_ERROR       (-1)

/* ────────── Public API ────────── */


/** Configure RX filter (range 0x101–0x12F) + enable RX FIFO0 IRQ.
 *  Call after HAL_FDCAN_Start(). */
void CAN_Handler_Init(void);

/** Process one pending CAN command (if any). Call from main loop. */
void CAN_Process(void);

/** Broadcast all telemetry frames at the given interval (3-phase staggered). */
int  CAN_Broadcast(uint32_t period_ms);

#endif /* CAN_HANDLER_H */
