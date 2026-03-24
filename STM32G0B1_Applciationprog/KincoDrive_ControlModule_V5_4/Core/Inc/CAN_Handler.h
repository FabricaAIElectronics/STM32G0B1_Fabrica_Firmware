/**
 * @file    CAN_Handler.h
 * @brief   CAN bus message dispatcher and protocol definitions.
 *
 * @details All CAN communication uses 29-bit Extended IDs with the layout:
 *
 *            [28:16]  Message type (function code)
 *            [15:0]   Device ID
 *
 *          Example: broadcast status (msg 0x600) from device 0x0667
 *                   => extended ID = 0x06000667
 *
 *          The device ID is compiled into this header as CAN_DEVICE_ID.
 *          Use the CAN_EXT_ID() macro to build a complete 29-bit ID from
 *          a message type constant.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Device identity
 * ════════════════════════════════════════════════════════════════════════════ */

#define CAN_DEVICE_ID           0x0667U     /* 16-bit device address */

/**
 * @brief  Build a 29-bit extended CAN ID from a message type.
 * @param  msg_type  Upper 13-bit function code (e.g. 0x600).
 * @retval 29-bit extended CAN identifier.
 */
#define CAN_EXT_ID(msg_type)    ((((uint32_t)(msg_type) & 0x1FFFU) << 16) | CAN_DEVICE_ID)

/**
 * @brief  Extract the message type from a 29-bit extended CAN ID.
 * @param  ext_id  Full 29-bit identifier.
 * @retval 13-bit message type.
 */
#define CAN_MSG_TYPE(ext_id)    (((ext_id) >> 16) & 0x1FFFU)

/**
 * @brief  Extract the device ID from a 29-bit extended CAN ID.
 * @param  ext_id  Full 29-bit identifier.
 * @retval 16-bit device address.
 */
#define CAN_GET_DEVICE(ext_id)  ((ext_id) & 0xFFFFU)

/* ════════════════════════════════════════════════════════════════════════════
 *  Message types — Commands (host → this device)
 * ════════════════════════════════════════════════════════════════════════════ */

#define MSG_SET_LED                 0x060U
#define MSG_SET_HS_DRIVE_POWER      0x110U
#define MSG_SET_HS_EXTRUDER_POWER   0x111U
#define MSG_SET_HS_SCRUBBING_POWER  0x112U
#define MSG_SET_HS_ENABLE_12V_BUCK  0x113U
#define MSG_SET_FAN_PWM             0x140U
#define MSG_SET_EEPROM_CONFIG       0x200U
#define MSG_READ_EEPROM_CONFIG      0x201U
#define MSG_DUMP_ERRORS             0x703U
#define MSG_RESET_ERROR             0x704U

/* ════════════════════════════════════════════════════════════════════════════
 *  Message types — Broadcasts / Responses (this device → host)
 * ════════════════════════════════════════════════════════════════════════════ */

#define MSG_BROADCAST_STATUS        0x600U  /* endstops + voltage + protection */
#define MSG_BROADCAST_CURRENTS      0x601U  /* bus + module currents */
#define MSG_BROADCAST_TEMPS         0x602U  /* thermistor temperatures */
#define MSG_BROADCAST_FANS          0x603U  /* fan speeds */
#define MSG_EEPROM_ACK              0x604U  /* EEPROM config response */
#define MSG_EEPROM_ACK2             0x605U  /* EEPROM config overflow */
#define MSG_ACK_GENERAL             0x700U  /* general ACK/NACK */
#define MSG_ERROR_DUMP              0x701U  /* error log dump */
#define MSG_ERROR_RESET_ACK         0x702U  /* error reset ACK/NACK */

/* ════════════════════════════════════════════════════════════════════════════
 *  Precomputed extended IDs (for use in code without runtime shifts)
 * ════════════════════════════════════════════════════════════════════════════ */

/* Commands (incoming — used for handler registration) */
#define CAN_ID_SET_LED                  CAN_EXT_ID(MSG_SET_LED)
#define CAN_ID_SET_HS_DRIVE_POWER       CAN_EXT_ID(MSG_SET_HS_DRIVE_POWER)
#define CAN_ID_SET_HS_EXTRUDER_POWER    CAN_EXT_ID(MSG_SET_HS_EXTRUDER_POWER)
#define CAN_ID_SET_HS_SCRUBBING_POWER   CAN_EXT_ID(MSG_SET_HS_SCRUBBING_POWER)
#define CAN_ID_SET_FAN_PWM              CAN_EXT_ID(MSG_SET_FAN_PWM)
#define CAN_ID_SET_EEPROM_CONFIG        CAN_EXT_ID(MSG_SET_EEPROM_CONFIG)
#define CAN_ID_READ_EEPROM_CONFIG       CAN_EXT_ID(MSG_READ_EEPROM_CONFIG)
#define CAN_ID_DUMP_ERRORS              CAN_EXT_ID(MSG_DUMP_ERRORS)
#define CAN_ID_RESET_ERROR              CAN_EXT_ID(MSG_RESET_ERROR)

/* Broadcasts / Responses (outgoing) */
#define CAN_ID_BROADCAST_STATUS         CAN_EXT_ID(MSG_BROADCAST_STATUS)
#define CAN_ID_BROADCAST_CURRENTS       CAN_EXT_ID(MSG_BROADCAST_CURRENTS)
#define CAN_ID_BROADCAST_TEMPS          CAN_EXT_ID(MSG_BROADCAST_TEMPS)
#define CAN_ID_BROADCAST_FANS           CAN_EXT_ID(MSG_BROADCAST_FANS)
#define CAN_ID_EEPROM_ACK              CAN_EXT_ID(MSG_EEPROM_ACK)
#define CAN_ID_EEPROM_ACK2             CAN_EXT_ID(MSG_EEPROM_ACK2)
#define CAN_ID_ACK_GENERAL              CAN_EXT_ID(MSG_ACK_GENERAL)
#define CAN_ID_ERROR_DUMP               CAN_EXT_ID(MSG_ERROR_DUMP)
#define CAN_ID_ERROR_RESET_ACK          CAN_EXT_ID(MSG_ERROR_RESET_ACK)

/* Bootloader entry: host sends to our device ID with payload[0] = 0xFF */
#define CAN_ID_BOOTLOADER               CAN_DEVICE_ID

/* ════════════════════════════════════════════════════════════════════════════
 *  Return codes
 * ════════════════════════════════════════════════════════════════════════════ */

#define CAN_SUCCESS     0
#define CAN_ERROR       (-1)

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Remap the vector table for bootloader compatibility.
 *         Must be called before HAL_Init().
 */
void Pre_CAN_Handler_Init(void);

/**
 * @brief  Register all CAN command handlers and activate RX notifications.
 *         Call after HAL_FDCAN_Start().
 */
void CAN_Handler_Init(void);

/**
 * @brief  Dequeue and dispatch one received CAN frame.
 *         Call from the main loop. Non-blocking — returns immediately
 *         if the receive ring buffer is empty.
 */
void CAN_Handler_Dispatch_Process_One(void);

/**
 * @brief  Periodically broadcast system telemetry over CAN.
 *
 * @details Rate-limited by an internal timestamp. When the period elapses,
 *          four frames are transmitted:
 *
 *          CAN_ID_BROADCAST_STATUS   (8 bytes) — endstops, voltages, protection
 *          CAN_ID_BROADCAST_CURRENTS (5 bytes) — bus + per-module currents
 *          CAN_ID_BROADCAST_TEMPS    (8 bytes) — thermistor temperatures
 *          CAN_ID_BROADCAST_FANS     (5 bytes) — fan speeds
 *
 * @param  period_ms  Minimum interval between broadcasts in milliseconds.
 * @retval CAN_SUCCESS always.
 *
 * @note   Call from main loop only (not ISR-safe).
 */
int CAN_Handler_Broadcast(uint32_t period_ms);

/**
 * @brief  Send a 2-byte ACK frame: { cmd_echo, 0x00 }.
 * @param  dst_id  Full 29-bit destination CAN ID.
 * @param  cmd     Command byte to echo back.
 */
void send_ack(uint32_t dst_id, uint8_t cmd);

/**
 * @brief  Send a 2-byte NACK frame: { cmd_echo, 0xFF }.
 * @param  dst_id  Full 29-bit destination CAN ID.
 * @param  cmd     Command byte to echo back.
 */
void send_nack(uint32_t dst_id, uint8_t cmd);

#endif /* CAN_HANDLER_H */
