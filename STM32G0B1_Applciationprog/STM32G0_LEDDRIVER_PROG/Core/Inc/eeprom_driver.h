/*
 * eeprom.h
 *
 *  Created on: 1 Dec 2025
 *      Author: jordan
 */

#ifndef INC_EEPROM_DRIVER_H_
#define INC_EEPROM_DRIVER_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>

/* magic bumped 0x3584 → 0x3585 when buck_mode field was added — old EEPROM
 * contents are rejected by checkcfg() and fall back to LoadDefault(). */
#define EEPROM_CFG_MAGIC   0x3585

typedef struct __attribute__((packed)) {
	uint16_t 	magic;
    uint16_t 	under_voltage_24;
    uint16_t    under_voltage_17_5;
    uint16_t 	pwm0;
    uint16_t 	pwm1;
    uint16_t    pwm2;
    uint8_t     buck_mode;     /* BUCK_MODE_t enum value (peripheral.h) */
    uint8_t     reserved;      /* alignment / future use */
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
#endif /* INC_EEPROM_DRIVER_H_ */
