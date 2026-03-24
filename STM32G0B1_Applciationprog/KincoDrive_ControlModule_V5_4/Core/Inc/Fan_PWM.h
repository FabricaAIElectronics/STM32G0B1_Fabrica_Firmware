/**
 * @file    Fan_PWM.h
 * @brief   Fan PWM speed control and tachometer-based speed measurement.
 *
 * @details Controls 5 PWM fan channels and reads tachometer feedback via
 *          DMA input capture.  Fan speed is reported as a percentage of
 *          the configurable max RPM stored in EEPROM.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#ifndef FAN_PWM_H
#define FAN_PWM_H

#include <stdint.h>
#include <stddef.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  Fan channel identifiers
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    FAN_DR = 1,     /* Drive */
    FAN_EP = 2,     /* Extruder Platform */
    FAN_EH = 3,     /* Extruder Head */
    FAN_ST = 4,     /* Stepper */
    FAN_SF = 5      /* Scrubbing Front */
} FanNumber_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

/** CAN command handler: set fan PWM from CAN payload. */
void CAN_Handle_set_Fan_PWM(uint32_t id, uint8_t *params, uint8_t len);

/** Set fan speed as percentage (0–100). */
void set_Fan_PWM(FanNumber_t fan_number, uint8_t speed_percent);

/** Start all PWM output channels. Call once during init. */
void start_all_Fan_PWM(void);

/** Start DMA input capture for all tachometer channels. */
void start_Fan_Tacho_DMA(void);

/**
 * @brief  Calculate fan speed as a percentage of max RPM.
 * @param  fan_number  Which fan to read (FAN_DR … FAN_SF).
 * @param  speed_pct   Output: 0–100 (%).  Set to 0 on error.
 */
void Fan_Tacho_Speed_Calculate(FanNumber_t fan_number, uint16_t *speed_pct);

/**
 * @brief  Pack fan speed (%) into one CAN byte.
 * @param  fan_number  Which fan to sample.
 * @param  out         Destination buffer (>= 1 byte).
 * @param  out_size    Size of destination buffer.
 * @retval Bytes written (1) or 0 on error.
 */
size_t CAN_Packer_Fan_Speed_1Byte(FanNumber_t fan_number, uint8_t *out, size_t out_size);

#endif /* FAN_PWM_H */
