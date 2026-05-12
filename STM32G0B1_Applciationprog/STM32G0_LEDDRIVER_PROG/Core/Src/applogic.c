/*
 * applogic.c
 *
 *  Created on: 23 Feb 2026
 *      Author: jordan
 */
#include "applogic.h"
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
static void BroadcastDeviceStatusEx(AppState state, ErrorCode errCode);
static void ApplyBuckMode(AppStateMachine *sm);
static void BroadcastTick(AppStateMachine *sm);

/* ---- Phased broadcast scheduler ---- */
/* Each phase fires every BCAST_PHASE_INTERVAL_MS and emits ONE frame, so the
 * 3-slot FDCAN TX FIFO has plenty of time to drain between sends. With 3
 * phases at 100 ms each, every broadcast goes out at ~3.3 Hz. */
#define BCAST_PHASE_INTERVAL_MS  100U

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

    /* Run telemetry every loop iteration regardless of state. The phased
     * scheduler inside BroadcastTick keeps TX traffic light enough that the
     * FIFO never fills; EEPROM data is therefore broadcast continuously,
     * including in STATE_ERROR / STATE_RECOVERY. */
    BroadcastTick(sm);
}

/*=========================================================================
 *  STATE_INIT
 *  - Trigger first ADC measurement (so V24 is available when LOAD_CONFIG
 *    runs ApplyBuckMode for the first time).
 *  - Initialize tick timers.
 *  - Transition to LOAD_CONFIG.
 *
 *  Buck is NOT touched here — ApplyBuckMode in State_Running takes care of
 *  it according to the loaded config (default BUCK_AUTO).
 *=========================================================================*/
static void State_Init(AppStateMachine *sm)
{
    TrigerADCMEasurement();

    /* Seed all tick timers to current tick */
    uint32_t now = HAL_GetTick();
    sm->lastAdcTick       = now;
    sm->lastCanStatusTick = now;
    sm->lastEEPROMTick    = now;

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

    /* Apply config to CAN runtime defaults (until VOLTAGESET/LIGHTSET overrides). */
    can_rxMessage.under_voltage_24   = sm->config.under_voltage_24;
    can_rxMessage.under_voltage_17_5 = sm->config.under_voltage_17_5;
    can_rxMessage.pwm[0]             = (uint16_t)sm->config.pwm0;
    can_rxMessage.pwm[1]             = (uint16_t)sm->config.pwm1;
    can_rxMessage.pwm[2]             = (uint16_t)sm->config.pwm2;
    can_rxMessage.buck_mode          = sm->config.buck_mode;

    /* Apply initial PWM from config */
    sm->ledCtrl.pwm[0] = (uint8_t)can_rxMessage.pwm[0];
    sm->ledCtrl.pwm[1] = (uint8_t)can_rxMessage.pwm[1];
    sm->ledCtrl.pwm[2] = (uint8_t)can_rxMessage.pwm[2];
    apply_pwm(&sm->ledCtrl);

    /* Read voltages so AUTO buck logic has a real V24 to compare on first call. */
    sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
    sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);
    ApplyBuckMode(sm);

    sm->state = STATE_RUNNING;
}

/*=========================================================================
 *  STATE_RUNNING
 *  Tick-driven, non-blocking. Every TICK_ADC_INTERVAL_MS the device:
 *    - Reads V24 and V17.5 from the most recent DMA transfer
 *    - Re-triggers DMA for next cycle
 *    - Re-evaluates the buck control mode (AUTO follows V24 vs threshold)
 *    - Runs the UV debounce; on persistent UV → STATE_ERROR
 *
 *  Telemetry broadcasts are handled by BroadcastTick() (called from
 *  AppLogic_Run after the state dispatch).
 *=========================================================================*/
static void State_Running(AppStateMachine *sm)
{
    if (can_rxMessage.flashdetected) {
        FOCdetection();
        return;
    }

    uint32_t now = HAL_GetTick();

    /* ---- 100 ms: ADC + buck control + UV check ---- */
    if (now - sm->lastAdcTick >= TICK_ADC_INTERVAL_MS) {
        sm->lastAdcTick = now;

        /* Read voltages from previous DMA transfer */
        sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
        sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);

        /* Trigger next ADC measurement for the next cycle */
        TrigerADCMEasurement();

        /* Apply buck control (AUTO follows the V24 threshold). */
        ApplyBuckMode(sm);

        /* Check undervoltage with debounce */
        ErrorCode err = CheckUndervoltage(sm);
        if (err != ERR_NONE) {
            sm->uvDebounceCount++;
            if (sm->uvDebounceCount >= UV_DEBOUNCE_COUNT) {
                sm->errorCode = err;
                sm->uvDebounceCount = 0;
                sm->state = STATE_ERROR;
                return;
            }
        } else {
            sm->uvDebounceCount = 0;
        }
    }

    /* Continuous: apply CAN RX commands + EEPROM save/reset. */
    ProcessCANCommands(sm);
    ProcessEEPROMCommands(sm);
}

/*=========================================================================
 *  STATE_ERROR
 *  - Buck disable is conditional: BUCK_MANUAL_ON keeps the buck on (user
 *    override for bench testing); the other two modes disable it.
 *  - All LEDs off.
 *  - ADC continues so we can detect recovery.
 *  - Telemetry broadcasts continue via BroadcastTick().
 *=========================================================================*/
static void State_Error(AppStateMachine *sm)
{
    static uint8_t enteredError = 0;
    uint32_t now = HAL_GetTick();

    /* One-time entry actions */
    if (!enteredError) {
        if (can_rxMessage.buck_mode != BUCK_MANUAL_ON) {
            SET_BUCK(DISABLE_BUCK);
        }

        /* Turn off all LEDs */
        sm->ledCtrl.pwm[0] = 0;
        sm->ledCtrl.pwm[1] = 0;
        sm->ledCtrl.pwm[2] = 0;
        apply_pwm(&sm->ledCtrl);

        enteredError = 1;
    }

    /* ---- 100ms: re-read ADC to detect recovery ---- */
    if (now - sm->lastAdcTick >= TICK_ADC_INTERVAL_MS) {
        sm->lastAdcTick = now;

        sm->ledStatus.voltage_24   = READADC(V24_CHANNEL);
        sm->ledStatus.voltage_17_5 = READADC(V17_5_CHANNEL);
        TrigerADCMEasurement();

        /* Buck control still applies — user can change mode mid-error via CAN. */
        ApplyBuckMode(sm);

        ErrorCode err = CheckUndervoltage(sm);
        if (err == ERR_NONE) {
            enteredError = 0;
            sm->recoveryEntryTick = now;
            sm->state = STATE_RECOVERY;
            return;
        }
    }

    /* CAN command processing remains active in error state so the host can
     * change UV thresholds, change buck mode, save EEPROM, etc. */
    ProcessCANCommands(sm);
    ProcessEEPROMCommands(sm);
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

    /* One-time entry: re-evaluate buck per current mode (don't blindly enable
     * if the host has set BUCK_MANUAL_OFF). */
    if (!buckReenabled) {
        ApplyBuckMode(sm);
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
            /* Recovered successfully -- restore last commanded PWM */
            sm->errorCode         = ERR_NONE;
            sm->lastAdcTick       = now;
            sm->lastCanStatusTick = now;
            sm->uvDebounceCount   = 0;
            buckReenabled         = 0;

            /* Re-apply PWM from last CAN values so LEDs turn back on */
            sm->ledCtrl.pwm[0] = (uint8_t)can_rxMessage.pwm[0];
            sm->ledCtrl.pwm[1] = (uint8_t)can_rxMessage.pwm[1];
            sm->ledCtrl.pwm[2] = (uint8_t)can_rxMessage.pwm[2];
            apply_pwm(&sm->ledCtrl);

            sm->state = STATE_RUNNING;
        } else {
            /* Still bad -- back to error */
            sm->errorCode = err;
            buckReenabled = 0;
            sm->state     = STATE_ERROR;
        }
    }

    /* Keep handling CAN commands during the recovery wait. */
    ProcessCANCommands(sm);
    ProcessEEPROMCommands(sm);
}

/*=========================================================================
 *  HELPER: ProcessCANCommands
 *  Apply PWM values from can_rxMessage (set by FDCAN RX ISR) to hardware.
 *=========================================================================*/
static void ProcessCANCommands(AppStateMachine *sm)
{	if(can_rxMessage.newcommandreceived==1){
    sm->ledCtrl.pwm[0] = (uint8_t)can_rxMessage.pwm[0];
    sm->ledCtrl.pwm[1] = (uint8_t)can_rxMessage.pwm[1];
    sm->ledCtrl.pwm[2] = (uint8_t)can_rxMessage.pwm[2];
    apply_pwm(&sm->ledCtrl);
}
can_rxMessage.newcommandreceived = 0;
}

/*=========================================================================
 *  HELPER: ProcessEEPROMCommands
 *  Handle eeprom_cmd flags set by CAN RX ISR (EEPROMSET 0x125).
 *=========================================================================*/
static void ProcessEEPROMCommands(AppStateMachine *sm)
{
    /* Write current running config to EEPROM */
    if (eeprom_cmd.write_eeprom_flag) {
        eeprom_cmd.write_eeprom_flag = 0;

        /* Build config from current running values */
        sm->config.magic              = EEPROM_CFG_MAGIC;
        sm->config.under_voltage_24   = can_rxMessage.under_voltage_24;
        sm->config.under_voltage_17_5 = can_rxMessage.under_voltage_17_5;
        sm->config.pwm0               = can_rxMessage.pwm[0];
        sm->config.pwm1               = can_rxMessage.pwm[1];
        sm->config.pwm2               = can_rxMessage.pwm[2];
        sm->config.buck_mode          = can_rxMessage.buck_mode;
        sm->config.reserved           = 0;

        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);
        broadcastEEPROMData(&sm->config);
    }

    /* Reset to factory defaults */
    if (eeprom_cmd.reset_default_flag) {
        eeprom_cmd.reset_default_flag = 0;

        LoadDefault(&sm->config);
        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET, &sm->config);

        /* Apply defaults to running state */
        can_rxMessage.under_voltage_24   = sm->config.under_voltage_24;
        can_rxMessage.under_voltage_17_5 = sm->config.under_voltage_17_5;
        can_rxMessage.pwm[0]             = (uint16_t)sm->config.pwm0;
        can_rxMessage.pwm[1]             = (uint16_t)sm->config.pwm1;
        can_rxMessage.pwm[2]             = (uint16_t)sm->config.pwm2;
        can_rxMessage.buck_mode          = sm->config.buck_mode;

        broadcastEEPROMData(&sm->config);
    }
}

/*=========================================================================
 *  HELPER: CheckUndervoltage
 *  Compare last-read ADC voltages against CAN-configured thresholds.
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

/*=========================================================================
 *  HELPER: BroadcastDeviceStatusEx
 *  Send actual state and error code on DEVSTATUS (0x128).
 *
 *  data[0] = system state:
 *            0x01=INIT, 0x02=LOAD_CONFIG, 0x03=RUNNING,
 *            0x04=ERROR, 0x05=RECOVERY
 *  data[1] = error code bitmask:
 *            Bit 0 = 24V undervoltage
 *            Bit 1 = 17.5V undervoltage
 *=========================================================================*/
static void BroadcastDeviceStatusEx(AppState state, ErrorCode errCode)
{
//    uint8_t data[2];
//
//    switch (state) {
//        case STATE_INIT:        data[0] = 0x01; break;
//        case STATE_LOAD_CONFIG: data[0] = 0x02; break;
//        case STATE_RUNNING:     data[0] = 0x03; break;
//        case STATE_ERROR:       data[0] = 0x04; break;
//        case STATE_RECOVERY:    data[0] = 0x05; break;
//        default:                data[0] = 0x00; break;
//    }

//    data[1] = (uint8_t)errCode;
    broadcastDeviceStatus(state,errCode);
//    CAN_Send(DEVSTATUS, data, 2);
}

/*=========================================================================
 *  HELPER: ApplyBuckMode
 *  Maps the current buck mode (set via VOLTAGESET) to the BUCK_EN GPIO.
 *
 *    BUCK_MANUAL_OFF — buck off
 *    BUCK_MANUAL_ON  — buck on regardless of voltage
 *    BUCK_AUTO       — buck on iff V24 >= under_voltage_24
 *                      (a threshold of 0 disables the check entirely → on)
 *=========================================================================*/
static void ApplyBuckMode(AppStateMachine *sm)
{
    switch (can_rxMessage.buck_mode) {
        case BUCK_MANUAL_OFF:
            SET_BUCK(DISABLE_BUCK);
            break;

        case BUCK_MANUAL_ON:
            SET_BUCK(ENABLE_BUCK);
            break;

        case BUCK_AUTO:
        default:
            if (can_rxMessage.under_voltage_24 == 0 ||
                sm->ledStatus.voltage_24 >= can_rxMessage.under_voltage_24) {
                SET_BUCK(ENABLE_BUCK);
            } else {
                SET_BUCK(DISABLE_BUCK);
            }
            break;
    }
}

/*=========================================================================
 *  HELPER: BroadcastTick
 *  Phased CAN telemetry — one frame per 100 ms tick. With three rotating
 *  phases (LIGHTSTATUS / DEVSTATUS / EEPROMDATA), each broadcast emits at
 *  ~3.3 Hz and the FDCAN TX FIFO never sees more than one fresh frame per
 *  ~100 ms (plenty of time for the previous frame to ACK and drain).
 *
 *  Runs in every state EXCEPT during a flash-in-progress window, so the
 *  host always sees a heartbeat of EEPROM contents to confirm reads/writes.
 *=========================================================================*/
static void BroadcastTick(AppStateMachine *sm)
{
    /* Suppress TX while OpenBLT is flashing (XCP polling pings detected). */
    if (can_rxMessage.flashdetected) return;

    static uint32_t last_tick = 0;
    static uint8_t  phase     = 0;

    uint32_t now = HAL_GetTick();
    if ((now - last_tick) < BCAST_PHASE_INTERVAL_MS) return;
    last_tick = now;

    /* Refresh status payload from latest CAN-set values before emitting. */
    sm->ledStatus.pwm[0] = (uint16_t)can_rxMessage.pwm[0];
    sm->ledStatus.pwm[1] = (uint16_t)can_rxMessage.pwm[1];
    sm->ledStatus.pwm[2] = (uint16_t)can_rxMessage.pwm[2];

    switch (phase) {
        case 0:
            braodcastLEDStatus(sm->ledStatus);
            break;
        case 1:
            BroadcastDeviceStatusEx(sm->state, sm->errorCode);
            break;
        case 2:
            broadcastEEPROMData(&sm->config);
            break;
        default:
            break;
    }

    if (++phase > 2U) phase = 0U;
}
