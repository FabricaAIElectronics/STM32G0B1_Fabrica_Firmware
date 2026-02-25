/*
 * applogic.h
 *
 *  Created on: 23 Feb 2026
 *      Author: jordan
 */

#ifndef INC_APPLOGIC_H_
#define INC_APPLOGIC_H_

#include "main.h"
#include "header.h"
#include "eeprom_driver.h"
#include "can_operation.h"
#include "peripheral.h"
#include <stdbool.h>

/* ---- State Definitions ---- */
typedef enum {
    STATE_INIT = 0,
    STATE_LOAD_CONFIG,
    STATE_RUNNING,
    STATE_ERROR,
    STATE_RECOVERY
} AppState;

/* ---- Error Code Bitmask ---- */
typedef enum {
    ERR_NONE              = 0x00,
    ERR_UNDERVOLTAGE_24   = 0x01,   /* Bit 0: 24V undervoltage */
    ERR_UNDERVOLTAGE_17_5 = 0x02    /* Bit 1: 17.5V undervoltage */
} ErrorCode;

/* ---- EEPROM Storage Location ---- */
#define EEPROM_CONFIG_PAGE    1
#define EEPROM_CONFIG_OFFSET  0

/* ---- Tick Intervals (ms) ---- */
#define TICK_ADC_INTERVAL_MS        100
#define TICK_CAN_STATUS_INTERVAL_MS 200
#define TICK_RECOVERY_DELAY_MS      200
#define TICK_EEPROM_DATA_MS			1000

/* ---- State Machine Context ---- */
typedef struct {
    AppState    state;
    ErrorCode   errorCode;

    /* Loaded EEPROM configuration */
    Config      config;

    /* Runtime peripheral context */
    AppContext   ledCtrl;
    LED_Peripheral_STATUS ledStatus;

    /* Non-blocking tick timers */
    uint32_t    lastAdcTick;
    uint32_t    lastCanStatusTick;
    uint32_t    recoveryEntryTick;
    uint32_t	lastEEPROMTick;

} AppStateMachine;

/* ---- Public API ---- */
void AppLogic_Init(AppStateMachine *sm);
void AppLogic_Run(AppStateMachine *sm);



#endif /* INC_APPLOGIC_H_ */
