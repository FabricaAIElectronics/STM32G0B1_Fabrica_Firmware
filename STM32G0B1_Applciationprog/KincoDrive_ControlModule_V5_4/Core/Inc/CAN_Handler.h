/**
 * @file    CAN_Handler.h
 * @brief   CAN RX mailbox + periodic broadcast.
 *
 * @details The CAN module is a pure data transport.  It does NOT dispatch
 *          commands or touch hardware — AppLogic pulls frames via
 *          CAN_TryGetFrame() and decides what to do with them.
 *
 *          All IDs are 11-bit standard.  Commands and broadcasts share a
 *          flat msg-type = ID numbering.
 *
 * @author  jordan
 * @date    2026-04-21
 */

#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Device identity (kept for bootloader trigger only)
 * ═══════════════════════════════════════════════════════════════════════ */

#define CAN_DEVICE_ID               0x667U

/* ═══════════════════════════════════════════════════════════════════════
 *  Commands  (Host → Device)
 * ═══════════════════════════════════════════════════════════════════════ */

#define MSG_CMD_HS_POWER            0x110U  /* DLC=1  bit0=DR bit1=E bit2=SC bit3=VBUCK */
#define MSG_CMD_FAN_PWM             0x140U  /* DLC=5  bytes DR/EP/EH/ST/SF  (0–100 %)   */
#define MSG_CMD_EEPROM              0x200U  /* DLC=1  0=load defaults  1=save state     */

/* ═══════════════════════════════════════════════════════════════════════
 *  Broadcasts  (Device → Host)
 * ═══════════════════════════════════════════════════════════════════════ */

#define MSG_BCAST_VOLTAGES          0x600U  /* 24V/12V bus (mV, u16 LE)         */
#define MSG_BCAST_CURRENTS          0x601U  /* bus+3 HS modules (mA, u16 LE)    */
#define MSG_BCAST_TEMPS             0x602U  /* 6x PTC thermistors (°C+40)       */
#define MSG_BCAST_FANS              0x603U  /* 5x fan tachometer (%)            */
#define MSG_BCAST_GPIO              0x604U  /* pin states + dbg counters        */

/* Bootloader trigger: host sends standard ID 0x667 with payload[0] = 0xFF */
#define CAN_ID_BOOTLOADER           CAN_DEVICE_ID

/* ═══════════════════════════════════════════════════════════════════════
 *  RX mailbox frame type
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint16_t id;            /* 11-bit standard ID                */
    uint8_t  data[8];       /* payload (zero-padded)             */
    uint8_t  dlc;           /* actual byte count, 0–8            */
} CAN_RxFrame_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  Return codes
 * ═══════════════════════════════════════════════════════════════════════ */

#define CAN_SUCCESS     0
#define CAN_ERROR       (-1)

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

/** Vector table remap for bootloader compatibility.  Call before HAL_Init(). */
void Pre_CAN_Init(void);

/** Configure filter + enable RX IT.  Call between MX_FDCAN1_Init() and
 *  HAL_FDCAN_Start(). */
void CAN_Init(void);

/**
 * @brief  Atomic, single-shot dequeue of the most recent RX frame.
 * @param  out  Destination (populated only if a frame is pending).
 * @retval true  Frame was pending — *out populated, mailbox cleared.
 * @retval false No frame pending.
 */
bool CAN_TryGetFrame(CAN_RxFrame_t *out);

/** Periodic telemetry broadcast.  Call from main loop. */
int  CAN_Broadcast(uint32_t period_ms);

#endif /* CAN_HANDLER_H */
