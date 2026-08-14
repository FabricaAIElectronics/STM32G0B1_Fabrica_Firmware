/*
 * eeprom_driver.h
 *
 * AT24C256 (IC1) on I2C1, device address 0xA0.
 * The page read/write primitives are shared verbatim with the LEDDriver and
 * PowerStage builds; only Config and LoadDefault() are board-specific.
 */

#ifndef INC_EEPROM_DRIVER_H_
#define INC_EEPROM_DRIVER_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>

/* Bump this whenever a field is added, moved or resized. checkcfg() rejects
 * any stored image whose magic does not match, and the caller then falls back
 * to LoadDefault() — which is what stops a struct from one firmware version
 * being reinterpreted, field-for-field wrongly, by the next. */
#define EEPROM_CFG_MAGIC   0x4B42U   /* 'KB' — knob / button board          */

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  default_led_mask;      /* LED_1..6 pattern applied at startup   */
    uint8_t  default_led_int_mask;  /* LED_INT_1..4 pattern applied at start */
    uint8_t  default_led_source;    /* LedSource: 0 = J1 direct, 1 = STM32   */
    uint8_t  flags;                 /* bit0 = invert encoder direction       */
    uint8_t  reserved[2];           /* keeps the struct at 8 bytes, so the   */
                                    /* whole config fits one CAN frame       */
} Config;

#define CFG_FLAG_ENCODER_INVERT   0x01U

typedef enum {
    EEPROM_IDLE = 0,
    EEPROM_ERASING,
    EEPROM_ERASING_DONE
} EEPROM_format_status;

void  EEPROM_Write(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);
void  EEPROM_Read(uint16_t page, uint16_t offset, uint8_t *data, uint16_t size);
uint32_t float2Bytes(float float_data);
float bytes2Float(uint8_t buffer[4]);
void  EEPROM_Write_Num(uint16_t page, uint16_t offset, float data);
float EEPROM_Read_Num(uint16_t page, uint16_t offset);
void  EEPROM_pageErase(uint16_t page);
void  EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg);
void  EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg);
void  LoadDefault(Config *config);
bool  checkcfg(Config *cfg);

#endif /* INC_EEPROM_DRIVER_H_ */
