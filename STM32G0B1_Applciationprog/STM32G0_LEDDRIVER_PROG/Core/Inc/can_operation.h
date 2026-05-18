#ifndef CAN_OPERATION_H
#define CAN_OPERATION_H
#include "stm32g0xx_hal.h"
#include "main.h"
#include "eeprom_driver.h"

/* canHandle is the FDCAN1 instance defined in main.c. The alias keeps the
 * existing call sites short — it must be kept in sync if main.c renames it. */
extern FDCAN_HandleTypeDef hfdcan1;


/* LEDDriver CAN IDs (11-bit standard, CANopen-safe gap 0x160..0x17F).
 *
 * DEVICEID (0x160) doubles as the OpenBLT bootloader RX ID:
 * one XCP CONNECT frame (byte[0]=0xFF, dlc=2) on 0x160 resets the running
 * application and is then re-handled by the bootloader on the same ID.
 * Must match STM32G0B1_Bootloader/G0B1_LEDDriver_Boot/App/blt_conf.h. */
#define LED_DEVICEID        0x160   /* RX: bootloader RX / app reset trigger        */
#define LED_CMD_LIGHTSET    0x170   /* RX: 3-channel PWM set, DLC=3                 */
#define LED_CMD_VOLTAGESET  0x171   /* RX: UV thresholds + buck mode, DLC=5         */
                                    /*   byte 0..1 V24 UV mV (BE)                   */
                                    /*   byte 2..3 V17.5 UV mV (BE)                 */
                                    /*   byte 4    buck mode (0=OFF 1=ON 2=AUTO)    */
                                    /*   shorter DLC keeps previous bytes' value    */
#define LED_CMD_EEPROMSET   0x172   /* RX: EEPROM save/load default, DLC=1          */
#define LED_BCAST_EEPROMDATA  0x178 /* TX: EEPROM config echo (DLC=8 incl. mode)    */
#define LED_BCAST_LIGHTSTATUS 0x179 /* TX: PWM + voltages telemetry                 */
#define LED_BCAST_DEVSTATUS   0x17A /* TX: state + error code                       */

/* Legacy aliases — keep until external tools migrate. Safe to remove later. */
#define DEVICEID    LED_DEVICEID
#define LIGHTSET    LED_CMD_LIGHTSET
#define VOLTAGESET  LED_CMD_VOLTAGESET
#define EEPROMSET   LED_CMD_EEPROMSET
#define EEPROMDATA  LED_BCAST_EEPROMDATA
#define LIGHTSTATUS LED_BCAST_LIGHTSTATUS
#define DEVSTATUS   LED_BCAST_DEVSTATUS
typedef struct {
    uint16_t 	under_voltage_24;
    uint16_t    under_voltage_17_5;
    uint16_t	pwm[3];
    uint8_t 	newcommandreceived;
    uint8_t		flashdetected;
    uint8_t		buck_mode;       /* BUCK_MODE_t enum, set by VOLTAGESET byte 4  */
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
void FOCdetection(void);

#endif /*CAN_OPERATION_H*/
