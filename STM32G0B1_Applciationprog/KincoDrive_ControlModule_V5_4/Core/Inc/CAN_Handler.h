/**
 * @file    CAN_Handler.h
 * @brief   CAN protocol definitions and public API.
 *
 * @details All CAN communication uses 29-bit Extended IDs:
 *
 *            [28:16]  Message type (function code)
 *            [15:0]   Device ID  (0x0667)
 *
 *          Example: broadcast status (msg 0x600) from device 0x0667
 *                   => extended ID = 0x06000667
 *
 * @author  jordan
 * @date    2026-03-26
 */

#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Device identity
 * ═══════════════════════════════════════════════════════════════════════ */

#define CAN_DEVICE_ID               0x0667U

/* Build a 29-bit extended CAN ID from a message type */
#define CAN_EXT_ID(msg_type)        ((((uint32_t)(msg_type) & 0x1FFFU) << 16) | CAN_DEVICE_ID)

/* Extract message type from a 29-bit extended CAN ID */
#define CAN_MSG_TYPE(ext_id)        (((ext_id) >> 16) & 0x1FFFU)

/* Extract device ID from a 29-bit extended CAN ID */
#define CAN_GET_DEVICE(ext_id)      ((ext_id) & 0xFFFFU)

/* ═══════════════════════════════════════════════════════════════════════
 *  Commands  (Host → Device)
 *
 *  Send a frame to CAN_EXT_ID(MSG_CMD_xxx) with the described payload.
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * MSG_CMD_HS_POWER  0x110  DLC=1
 *   Byte 0 bitmask: bit0=Drive  bit1=Extruder  bit2=Scrubbing  bit3=12V Buck
 *   0=OFF, 1=ON for each bit.
 */
#define MSG_CMD_HS_POWER            0x110

/*
 * MSG_CMD_FAN_PWM  0x140  DLC=5
 *   Byte 0=Fan_DR  Byte 1=Fan_EP  Byte 2=Fan_EH  Byte 3=Fan_ST  Byte 4=Fan_SF
 *   Values 0–100 (%).
 */
#define MSG_CMD_FAN_PWM             0x140

/*
 * MSG_CMD_EEPROM  0x200  DLC=1
 *   Byte 0:  0 = Load hard-coded safe defaults (all OFF) and apply
 *            1 = Save current HS/fan state to EEPROM
 */
#define MSG_CMD_EEPROM              0x200

/* ═══════════════════════════════════════════════════════════════════════
 *  Broadcasts  (Device → Host, every 500 ms)
 * ═══════════════════════════════════════════════════════════════════════ */

#define MSG_BCAST_STATUS            0x600   /* voltages (mV) + endstop/ESTOP     */
#define MSG_BCAST_CURRENTS          0x601   /* currents (mA), DLC=8              */
#define MSG_BCAST_TEMPS             0x602   /* 6x PTC thermistors                */
#define MSG_BCAST_FANS              0x603   /* 5x fan tachometer %               */
#define MSG_BCAST_GPIO              0x604   /* HS/ESTOP/endstop pin states       */
#define MSG_BCAST_RAW_ADC           0x605   /* 12 ADC channels (4-bit each)      */
#define MSG_BCAST_CONFIG            0x606   /* EEPROM startup config             */

/* ═══════════════════════════════════════════════════════════════════════
 *  Precomputed extended IDs  (avoid runtime shifts)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Commands */
#define CAN_ID_CMD_HS_POWER         CAN_EXT_ID(MSG_CMD_HS_POWER)
#define CAN_ID_CMD_FAN_PWM          CAN_EXT_ID(MSG_CMD_FAN_PWM)
#define CAN_ID_CMD_EEPROM           CAN_EXT_ID(MSG_CMD_EEPROM)

/* Broadcasts */
#define CAN_ID_BCAST_STATUS         CAN_EXT_ID(MSG_BCAST_STATUS)
#define CAN_ID_BCAST_CURRENTS       CAN_EXT_ID(MSG_BCAST_CURRENTS)
#define CAN_ID_BCAST_TEMPS          CAN_EXT_ID(MSG_BCAST_TEMPS)
#define CAN_ID_BCAST_FANS           CAN_EXT_ID(MSG_BCAST_FANS)
#define CAN_ID_BCAST_GPIO           CAN_EXT_ID(MSG_BCAST_GPIO)
#define CAN_ID_BCAST_RAW_ADC        CAN_EXT_ID(MSG_BCAST_RAW_ADC)
#define CAN_ID_BCAST_CONFIG         CAN_EXT_ID(MSG_BCAST_CONFIG)

/* Bootloader: host sends to device ID with payload[0] = 0xFF */
#define CAN_ID_BOOTLOADER           CAN_DEVICE_ID

/* ═══════════════════════════════════════════════════════════════════════
 *  Return codes
 * ═══════════════════════════════════════════════════════════════════════ */

#define CAN_SUCCESS     0
#define CAN_ERROR       (-1)

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

/** Remap vector table for bootloader compatibility. Call before HAL_Init(). */
void Pre_CAN_Handler_Init(void);

/** Configure CAN filters and enable RX interrupt. Call after HAL_FDCAN_Start(). */
void CAN_Handler_Init(void);

/** Process one pending CAN command (if any). Call from main loop. */
void CAN_Process(void);

/** Broadcast all telemetry frames at the given interval. Call from main loop. */
int  CAN_Broadcast(uint32_t period_ms);

#endif /* CAN_HANDLER_H */
