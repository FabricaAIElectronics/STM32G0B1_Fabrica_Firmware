/*
 * eeprom.h
 *
 *  Created on: 1 Dec 2025
 *      Author: jordan
 */

#ifndef INC_EEPROM_H_
#define INC_EEPROM_H_

#include "stm32g0xx_hal.h"

typedef struct __attribute__((packed)) {
    uint8_t mode;
    uint16_t voltage;
    uint16_t count;
    uint16_t pwm0;
    uint16_t pwm1;
} Config;

void EEPROM_Write (uint16_t page,uint16_t offset, uint8_t *data,uint16_t size);
void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data,uint16_t size);
uint32_t float2Bytes(float float_data);
float bytes2Float(uint8_t buffer[4]);
void EEPROM_Write_Num(uint16_t page, uint16_t offset, float data);
float EEPROM_Read_Num(uint16_t page,uint16_t offset);
void EEPROM_pageErase(uint16_t page);
void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg);
void EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg);


#endif /* INC_EEPROM_H_ */
