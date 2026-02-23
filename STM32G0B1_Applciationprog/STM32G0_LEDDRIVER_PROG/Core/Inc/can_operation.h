#ifndef CAN_OPERATION_H
#define CAN_OPERATION_H
#include "stm32g0xx_hal.h"
#include "main.h"
#include "header.h"
#include "eeprom_driver.h"
extern FDCAN_HandleTypeDef canHandle;
#define DEVICEID 0x667
#define LIGHTSET 0x123
#define LIGHTSTATUS 0x127
#define DEVSTATUS 0x128
#define VOLTAGESET 0x124
#define EEPROMSET 0x125
#define EEPROMDATA 0x126
typedef struct {
    uint16_t 	under_voltage_24;
    uint16_t    under_voltage_17_5;
    uint16_t	pwm[3];
} CAN_RXMessage;

extern CAN_RXMessage can_rxMessage;

typedef struct {
    uint16_t 	voltage_24;
    uint16_t   	voltage_17_5;
    uint16_t	pwm[3];
} LED_Peripheral_STATUS;

extern LED_Peripheral_STATUS status;


typedef struct {
    uint8_t write_eeprom_flag;
    uint8_t reset_default_flag;
} eeprom_command;

extern eeprom_command eeprom_cmd;

void CAN_Init(void);
void CAN_Send(uint32_t id, uint8_t* data, uint8_t len);
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);
void broadcastEEPROMData(Config *config);
void braodcastLEDStatus(LED_Peripheral_STATUS status);
void broadcastDeviceStatus(uint8_t state, uint8_t errorCode);

#endif /*CAN_OPERATION_H*/
