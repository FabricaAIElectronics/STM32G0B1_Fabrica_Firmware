/*
 * applogic.c
 *
 *  Created on: 23 Feb 2026
 *      Author: jordan
 */
#include "applogic.h"
#include "canopen_bridge.h"
#include <string.h>

/* ---- Static Handler Prototypes ---- */
static void State_Init(AppStateMachine *sm);
static void State_LoadConfig(AppStateMachine *sm);
static void State_Running(AppStateMachine *sm);
static void State_Error(AppStateMachine *sm);
static void State_Recovery(AppStateMachine *sm);

/* ---- Helper Prototypes ---- */
static void ProcessCANCommands(AppStateMachine *sm);
static void ProcessEEPROMCommands(AppStateMachine *sm);
static ErrorCode CheckUndervoltage(AppStateMachine *sm);

/*=========================================================================
 *  AppLogic_Init
 *  Called once before the main while loop.
 *  Zeroes the state machine struct and sets initial state.
 *=========================================================================*/
void AppLogic_Init(AppStateMachine *sm)
{
    memset(sm, 0, sizeof(AppStateMachine));
    sm->state = STATE_INIT;
    sm->errorCode = ERR_NONE;
}

/*=========================================================================
 *  AppLogic_Run
 *  Called every iteration of the main while(1) loop.
 *  Dispatches to the current state handler.
 *=========================================================================*/
void AppLogic_Run(AppStateMachine *sm)
{
    switch (sm->state) {
        case STATE_INIT:
            State_Init(sm);
            break;
        case STATE_LOAD_CONFIG:
            State_LoadConfig(sm);
            break;
        case STATE_RUNNING:
            State_Running(sm);
            break;
        case STATE_ERROR:
            State_Error(sm);
            break;
        case STATE_RECOVERY:
            State_Recovery(sm);
            break;
        default:
            sm->state = STATE_INIT;
            break;
    }
}

/*=========================================================================
 *  STATE_INIT
 *  - Enable buck converter
 *  - Trigger first ADC measurement
 *  - Initialize tick timers
 *  - Transition to LOAD_CONFIG
 *=========================================================================*/
static void State_Init(AppStateMachine *sm)
{
    SET_BUCK(ENABLE_BUCK);
    TrigerADCMEasurement();

    /* Seed all tick timers to current tick */
    uint32_t now = HAL_GetTick();
    sm->lastAdcTick       = now;
    sm->lastCanStatusTick = now;

    sm->state = STATE_LOAD_CONFIG;
}

/*=========================================================================
 *  STATE_LOAD_CONFIG
 *  - Read Config struct from EEPROM page 1, offset 0
 *  - Validate magic number (0x3584)
 *  - If valid: apply config values, go to RUNNING
 *  - If invalid: load defaults, write defaults to EEPROM, go to RUNNING
 *=========================================================================*/
static void State_LoadConfig(AppStateMachine *sm)
{
    EEPROM_Read_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);

    if (checkcfg(&sm->config)) {
        /* Valid config found in EEPROM */
    } else {
        /* Invalid or blank EEPROM -- load defaults and persist */
        LoadDefault(&sm->config);
        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);
    }

    /* Apply config to CAN thresholds (used as runtime defaults
       until an SDO write overrides them) */
    can_rxMessage.under_voltage_24   = sm->config.under_voltage_24;
    can_rxMessage.under_voltage_17_5 = sm->config.under_voltage_17_5;
    can_rxMessage.pwm[0] = (uint16_t)sm->config.pwm0;
    can_rxMessage.pwm[1] = (uint16_t)sm->config.pwm1;
    can_rxMessage.pwm[2] = (uint16_t)sm->config.pwm2;

    /* Apply initial PWM from config */
    sm->ledCtrl.pwm[0] = (uint8_t)can_rxMessage.pwm[0];
    sm->ledCtrl.pwm[1] = (uint8_t)can_rxMessage.pwm[1];
    sm->ledCtrl.pwm[2] = (uint8_t)can_rxMessage.pwm[2];
    apply_pwm(&sm->ledCtrl);

    sm->state = STATE_RUNNING;
}

/*=========================================================================
 *  STATE_RUNNING
 *  Non-blocking tick-driven main operation:
 *
 *  Every loop iteration:
 *    - Process CANopen RX frames (NMT + SDO)
 *    - Transmit queued CANopen TX frames
 *    - Apply latest SDO-written PWM values
 *    - Handle EEPROM commands
 *
 *  Every 100ms:
 *    - Read ADC voltage values
 *    - Check undervoltage thresholds
 *
 *  Every 500ms:
 *    - Send CANopen heartbeat
 *=========================================================================*/
static void State_Running(AppStateMachine *sm)
{
    if (can_rxMessage.flashdetected == 0) {
        uint32_t now = HAL_GetTick();

        /* ---- CANopen: process RX and send TX every iteration ---- */
        CanOpenBridge_ProcessRx();
        CanOpenBridge_SendPending();

        /* ---- 100ms: ADC + undervoltage check ---- */
        if (now - sm->lastAdcTick >= TICK_ADC_INTERVAL_MS) {
            sm->lastAdcTick = now;

            /* Read voltages from previous DMA transfer */
            sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
            sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);

            /* Check undervoltage */
            ErrorCode err = CheckUndervoltage(sm);
            if (err != ERR_NONE) {
                sm->errorCode = err;
                sm->state = STATE_ERROR;
                return;
            }
        }

        /* ---- 200ms: CANopen heartbeat ---- */
        if (now - sm->lastCanStatusTick >= TICK_CAN_STATUS_INTERVAL_MS) {
            sm->lastCanStatusTick = now;

            /* Update PWM values in status */
            sm->ledStatus.pwm[0] = can_rxMessage.pwm[0];
            sm->ledStatus.pwm[1] = can_rxMessage.pwm[1];
            sm->ledStatus.pwm[2] = can_rxMessage.pwm[2];

            CanOpenBridge_SendHeartbeat();
        }

        /* ---- Continuous: process CAN RX + EEPROM commands ---- */
        ProcessCANCommands(sm);
        ProcessEEPROMCommands(sm);
    } else {
        FOCdetection();
    }
}

/*=========================================================================
 *  STATE_ERROR
 *  - Disable buck converter (protect hardware)
 *  - Set all PWM to 0
 *  - Send heartbeat every 500ms
 *  - Continue ADC reads every 100ms to detect recovery
 *=========================================================================*/
static void State_Error(AppStateMachine *sm)
{
    static uint8_t enteredError = 0;
    uint32_t now = HAL_GetTick();
    can_rxMessage.newcommandreceived = 0;
    /* One-time entry actions */
    if (!enteredError) {
        SET_BUCK(DISABLE_BUCK);

        /* Turn off all LEDs */
        sm->ledCtrl.pwm[0] = 0;
        sm->ledCtrl.pwm[1] = 0;
        sm->ledCtrl.pwm[2] = 0;
        apply_pwm(&sm->ledCtrl);

        enteredError = 1;
    }

    /* ---- CANopen: keep processing RX/TX even in error state ---- */
    CanOpenBridge_ProcessRx();
    CanOpenBridge_SendPending();

    /* ---- 100ms: re-read ADC to detect recovery ---- */
    if (now - sm->lastAdcTick >= TICK_ADC_INTERVAL_MS) {
        sm->lastAdcTick = now;

        sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
        sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);
        TrigerADCMEasurement();

        /* Check if voltage has recovered */
        ErrorCode err = CheckUndervoltage(sm);
        if (err == ERR_NONE) {
            enteredError = 0;
            sm->recoveryEntryTick = now;
            sm->state = STATE_RECOVERY;
            return;
        }
    }

    /* ---- 500ms: heartbeat ---- */
    if (now - sm->lastCanStatusTick >= TICK_CAN_STATUS_INTERVAL_MS) {
        sm->lastCanStatusTick = now;
        CanOpenBridge_SendHeartbeat();
    }
}

/*=========================================================================
 *  STATE_RECOVERY
 *  - Re-enable buck converter
 *  - Wait TICK_RECOVERY_DELAY_MS (200ms) non-blocking
 *  - Re-check voltage
 *    - If OK -> clear error code, go to RUNNING
 *    - If still bad -> go back to ERROR
 *=========================================================================*/
static void State_Recovery(AppStateMachine *sm)
{
    static uint8_t buckReenabled = 0;
    uint32_t now = HAL_GetTick();

    /* One-time entry: re-enable buck */
    if (!buckReenabled) {
        SET_BUCK(ENABLE_BUCK);
        TrigerADCMEasurement();
        buckReenabled = 1;
    }

    /* Wait for recovery delay to elapse */
    if (now - sm->recoveryEntryTick >= TICK_RECOVERY_DELAY_MS) {
        /* Read voltage after stabilization period */
        sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
        sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);

        ErrorCode err = CheckUndervoltage(sm);
        if (err == ERR_NONE) {
            /* Recovered successfully */
            sm->errorCode = ERR_NONE;
            sm->lastAdcTick       = now;
            sm->lastCanStatusTick = now;
            buckReenabled = 0;
            sm->state = STATE_RUNNING;
        } else {
            /* Still bad -- back to error */
            sm->errorCode = err;
            buckReenabled = 0;
            sm->state = STATE_ERROR;
        }
    }
}

/*=========================================================================
 *  HELPER: ProcessCANCommands
 *  Apply PWM values from can_rxMessage (set by SDO write callbacks) to hardware.
 *=========================================================================*/
static void ProcessCANCommands(AppStateMachine *sm)
{
    if (can_rxMessage.newcommandreceived) {
        sm->ledCtrl.pwm[0] = (uint8_t)can_rxMessage.pwm[0];
        sm->ledCtrl.pwm[1] = (uint8_t)can_rxMessage.pwm[1];
        sm->ledCtrl.pwm[2] = (uint8_t)can_rxMessage.pwm[2];
        apply_pwm(&sm->ledCtrl);
    }
    can_rxMessage.newcommandreceived = 0;
}

/*=========================================================================
 *  HELPER: ProcessEEPROMCommands
 *  Handle eeprom_cmd flags set by SDO write callback (0x6004).
 *=========================================================================*/
static void ProcessEEPROMCommands(AppStateMachine *sm)
{
    /* Write current running config to EEPROM */
    if (eeprom_cmd.write_eeprom_flag) {
        eeprom_cmd.write_eeprom_flag = 0;

        /* Build config from current running values */
        sm->config.magic              = 0x3584;
        sm->config.under_voltage_24   = can_rxMessage.under_voltage_24;
        sm->config.under_voltage_17_5 = can_rxMessage.under_voltage_17_5;
        sm->config.pwm0               = can_rxMessage.pwm[0];
        sm->config.pwm1               = can_rxMessage.pwm[1];
        sm->config.pwm2               = can_rxMessage.pwm[2];

        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);
    }

    /* Reset to factory defaults */
    if (eeprom_cmd.reset_default_flag) {
        eeprom_cmd.reset_default_flag = 0;

        LoadDefault(&sm->config);
        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);
        State_LoadConfig(sm);
        /* Apply defaults to running state */
        can_rxMessage.under_voltage_24   = sm->config.under_voltage_24;
        can_rxMessage.under_voltage_17_5 = sm->config.under_voltage_17_5;
        can_rxMessage.pwm[0] = (uint16_t)sm->config.pwm0;
        can_rxMessage.pwm[1] = (uint16_t)sm->config.pwm1;
        can_rxMessage.pwm[2] = (uint16_t)sm->config.pwm2;
    }
}

/*=========================================================================
 *  HELPER: CheckUndervoltage
 *  Compare last-read ADC voltages against SDO-configured thresholds.
 *  Threshold of 0 means "check disabled".
 *  Returns a bitmask ErrorCode.
 *=========================================================================*/
static ErrorCode CheckUndervoltage(AppStateMachine *sm)
{
    ErrorCode err = ERR_NONE;

    if (can_rxMessage.under_voltage_24 > 0) {
        if (sm->ledStatus.voltage_24 < can_rxMessage.under_voltage_24) {
            err |= ERR_UNDERVOLTAGE_24;
        }
    }

    if (can_rxMessage.under_voltage_17_5 > 0) {
        if (sm->ledStatus.voltage_17_5 < can_rxMessage.under_voltage_17_5) {
            err |= ERR_UNDERVOLTAGE_17_5;
        }
    }

    return err;
}
