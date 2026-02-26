/*
 * eeprom.h
 *
 *  Created on: 1 Dec 2025
 *      Author: jordan
 */

#ifndef INC_EEPROM_H_
#define INC_EEPROM_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t    magic;
    uint8_t     mode;
    uint16_t    voltage;
    uint16_t    count;
    uint16_t    pwm0;
    uint16_t    pwm1;
    uint16_t    hard_over_voltage;
    uint16_t    soft_over_voltage;
    uint16_t    under_voltage;
    uint16_t    over_current;
    uint16_t    fan_max_rpm;        /* max fan RPM for % calculation (e.g. 5000) */
} Config;
typedef enum {
	EEPROM_IDLE=0,
	EEPROM_ERASING,
	EEPROM_ERASING_DONE
}EEPROM_format_status;

void EEPROM_Write (uint16_t page,uint16_t offset, uint8_t *data,uint16_t size);
void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data,uint16_t size);
uint32_t float2Bytes(float float_data);
float bytes2Float(uint8_t buffer[4]);
void EEPROM_Write_Num(uint16_t page, uint16_t offset, float data);
float EEPROM_Read_Num(uint16_t page,uint16_t offset);
void EEPROM_pageErase(uint16_t page);
void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg);
void EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg);
void LoadDefault(Config *config);
bool checkcfg(Config *cfg);


#define EEPROM_SETTING_HARD_OV      0x00
#define EEPROM_SETTING_SOFT_OV      0x01
#define EEPROM_SETTING_UNDER_V      0x02
#define EEPROM_SETTING_OVER_CURR    0x03
#define EEPROM_SETTING_FAN_MAX_RPM  0x04


/**
  * @brief  CAN handler to write a single EEPROM config setting.
  *
  * CAN ID: 0x200
  * Payload (3 bytes):
  *   byte 0:   setting selector
  *               0x00 = hard_over_voltage
  *               0x01 = soft_over_voltage
  *               0x02 = under_voltage
  *               0x03 = over_current
  *               0x04 = fan_max_rpm
  *   byte 1–2: new value (uint16_t little-endian)
  *
  * Reads existing config first to preserve other fields.
  * Rejects value of 0 or > 60000 (fan_max_rpm capped at 30000).
  * Sends ACK on 0x604 with: byte 0 = selector, byte 1–2 = written value.
  */
void CAN_Handler_EEPROM_Write_Config(uint32_t id, uint8_t *params, uint8_t len);


/**
  * @brief  CAN handler to read and publish all EEPROM config settings.
  *
  * CAN ID: 0x201 (any payload or empty)
  * Response on 0x604 (8 bytes):
  *
  *   ┌──────────┬──────────────────────────────┬────────────┐
  *   │ Byte     │ Content                      │ Encoding   │
  *   ├──────────┼──────────────────────────────┼────────────┤
  *   │ 0–1      │ hard_over_voltage            │ u16 LE mV  │
  *   │ 2–3      │ soft_over_voltage            │ u16 LE mV  │
  *   │ 4–5      │ over_current                 │ u16 LE mA  │
  *   │ 6–7      │ fan_max_rpm                  │ u16 LE RPM │
  *   └──────────┴──────────────────────────────┴────────────┘
  *
  * under_voltage is sent in a second frame on 0x605 (2 bytes):
  *   byte 0–1: under_voltage (u16 LE mV)
  *
  * If no valid config exists, all bytes are 0.
  */
void CAN_Handler_EEPROM_Read_Config(uint32_t id, uint8_t *params, uint8_t len);


void EEPROM_Init(void);
void CAN_Handler_EEPROM_Write_Overvoltage_Config(uint32_t id, uint8_t *params, uint8_t len);
const Config* EEPROM_GetCachedConfig(void);
#endif /* INC_EEPROM_H_ */