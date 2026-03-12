#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H



#define CMD_SET_HS_DRIVE_POWER 0x110
#define CMD_SET_HS_EXTRUDER_POWER 0x111
#define CMD_SET_HS_SCRUBBING_POWER 0x112

#define CMD_SET_HS_ENABLE_12V_BUCK 0x113

#define CMD_SET_FAN_PWM 0x140

#define CMD_SET_EEPROM_CONFIG   0x200
#define CMD_READ_EEPROM_CONFIG  0x201

#define CMD_DUMP_ERRORS  0x703
#define CMD_RESET_ERROR  0x704

#define CMD_SET_LED 0x060




#include <stdint.h>


void Pre_CAN_Handler_Init(void);

void CAN_Handler_Init(void);

void CAN_Handler_Dispatch_Process_One(void);


/**
  * @brief  Periodically broadcast all system telemetry over CAN bus.
  *
  * @details This function is intended to be called every main loop iteration.
  *          It uses an internal static timestamp to rate-limit transmissions
  *          to the specified period. When the period elapses, it packs and
  *          transmits four CAN frames:
  *
  *          0x600 — System Status (8 bytes)
  *          ┌──────────┬──────────────────────────────────────────┬───────────┐
  *          │ Byte     │ Content                                  │ Encoding  │
  *          ├──────────┼──────────────────────────────────────────┼───────────┤
  *          │ 0        │ Endstop triggered state                  │ 1 bit/ch  │
  *          │ 1        │ Endstop fault state                      │ 1 bit/ch  │
  *          │ 2–3      │ 24V bus voltage                          │ u16 LE 0.1V │
  *          │ 4        │ 12V bus voltage                          │ u8 0.1V   │
  *          │ 5        │ Protection state (OC + OV)               │ bitfield  │
  *          │ 6–7      │ Reserved                                 │ 0x00      │
  *          └──────────┴──────────────────────────────────────────┴───────────┘
  *
  *          0x601 — Currents (5 bytes)
  *          ┌──────────┬──────────────────────────────────────────┬───────────┐
  *          │ Byte     │ Content                                  │ Encoding  │
  *          ├──────────┼──────────────────────────────────────────┼───────────┤
  *          │ 0–1      │ 24V bus current                          │ u16 LE 0.1A │
  *          │ 2        │ Drive module current                     │ u8 0.1A   │
  *          │ 3        │ Extruder module current                  │ u8 0.1A   │
  *          │ 4        │ Scrubbing module current                 │ u8 0.1A   │
  *          └──────────┴──────────────────────────────────────────┴───────────┘
  *
  *          0x602 — Temperatures (8 bytes)
  *          ┌──────────┬──────────────────────────────────────────┬───────────┐
  *          │ Byte     │ Content                                  │ Encoding  │
  *          ├──────────┼──────────────────────────────────────────┼───────────┤
  *          │ 0        │ Thermistor PTC 1 temperature             │ °C + 40   │
  *          │ 1        │ Thermistor PTC 2 temperature             │ °C + 40   │
  *          │ 2        │ Thermistor PTC 3 temperature             │ °C + 40   │
  *          │ 3        │ Thermistor PTC 4 temperature             │ °C + 40   │
  *          │ 4        │ Thermistor PTC 5 temperature             │ °C + 40   │
  *          │ 5        │ Thermistor PTC 6 temperature             │ °C + 40   │
  *          │ 6–7      │ Reserved                                 │ 0x00      │
  *          └──────────┴──────────────────────────────────────────┴───────────┘
  *
  *          0x603 — Fan Speeds (5 bytes)
  *          ┌──────────┬──────────────────────────────────────────┬───────────┐
  *          │ Byte     │ Content                                  │ Encoding  │
  *          ├──────────┼──────────────────────────────────────────┼───────────┤
  *          │ 0        │ Fan DR speed                             │ 0–100%    │
  *          │ 1        │ Fan EP speed                             │ 0–100%    │
  *          │ 2        │ Fan EH speed                             │ 0–100%    │
  *          │ 3        │ Fan ST speed                             │ 0–100%    │
  *          │ 4        │ Fan SF speed                             │ 0–100%    │
  *          └──────────┴──────────────────────────────────────────┴───────────┘
  *
  * @param  period_ms  Minimum interval between broadcasts in milliseconds.
  *                    Typical value: 100 (10 Hz) or 200 (5 Hz).
  *
  * @retval CAN_SUCCESS (0) Always returns success. Individual frame
  *         packing failures result in zero-filled bytes for that field;
  *         transmission errors are silently ignored.
  *
  * @note   - Call from main loop only (not ISR-safe).
  *         - Uses HAL_GetTick() for timing; requires SysTick to be running.
  *         - All multi-byte values are little-endian.
  *         - Temperature offset encoding: wire_value = actual_°C + 40
  *           (receiver decodes: temp_°C = wire_value − 40).
  *         - Protection state byte layout:
  *             bits [1:0] = overcurrent  (0=OK, 1=overcurrent)
  *             bits [3:2] = overvoltage  (0=OK, 1=soft, 2=hard)
  *             bits [7:4] = reserved
  */
int CAN_Handler_Broadcast(uint32_t period_ms);

void send_ack(uint32_t dst_id, uint8_t cmd);

void send_nack(uint32_t dst_id, uint8_t cmd);

#endif /* CAN_HANDLER_H */