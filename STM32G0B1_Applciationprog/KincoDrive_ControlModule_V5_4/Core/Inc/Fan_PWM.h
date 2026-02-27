#ifndef FAN_PWM_H_
#define FAN_PWM_H_

#include <stdint.h>
#include "main.h"
// Struct to represent a PWM Channel

typedef enum {
    FAN_DR = 1,
    FAN_EP = 2,
    FAN_EH = 3,
    FAN_ST = 4,
    FAN_SF = 5
} FanNumber_t;



void CAN_Handle_set_Fan_PWM(uint32_t id, uint8_t *params, uint8_t len);

// Set a fan speed as percentage (0-100).
// fan_number: 1..5 selecting FAN_PWM_1..FAN_PWM_5
// speed_percent: 0 (stopped) .. 100 (full scale)
void set_Fan_PWM(FanNumber_t fan_number, uint8_t speed_percent);

// Start all configured fan PWM channels (safe to call multiple times)
void start_all_Fan_PWM(void);

void start_Fan_Tacho_DMA(void);

/**
  * @brief  Calculate fan speed as a percentage of the configurable max RPM.
  * @param  fan_number  Which fan to read (FAN_DR … FAN_SF).
  * @param  speed_pct   Output: 0–100 (%). Clamped to 100 if measured RPM exceeds max.
  *                     Set to 0 on error (invalid fan, zero ticks, no valid config).
  *
  * @details
  * - Reads two consecutive DMA capture values to compute the period in timer ticks.
  * - Converts ticks → frequency → RPM (assumes 2 pulses per revolution).
  * - Divides by fan_max_rpm from EEPROM config to get a percentage.
  * - fan_max_rpm is configurable via CAN/EEPROM (default 5000).
  */
void Fan_Tacho_Speed_Calculate(FanNumber_t fan_number, uint16_t *speed_rpm_100s);

/**
  * @brief  Pack fan speed (%) into one CAN byte.
  * @param  fan_number  Which fan to sample.
  * @param  out         Destination buffer (>= 1 byte).
  * @param  out_size    Size of destination buffer.
  * @retval Bytes written (1) or 0 on error.
  *
  * Wire value: 0–100 representing fan speed as percentage of fan_max_rpm.
  */
size_t CAN_Packer_Fan_Speed_1Byte(FanNumber_t fan_number, uint8_t *out, size_t out_size);

#endif
