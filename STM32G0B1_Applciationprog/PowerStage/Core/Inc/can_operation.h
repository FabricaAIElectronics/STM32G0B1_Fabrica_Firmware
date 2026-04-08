/*
 * can_operation.h
 *
 *  Created on: Oct 31, 2025
 *      Author: jordan
 */

#ifndef INC_CAN_OPERATION_H_
#define INC_CAN_OPERATION_H_

#include "main.h"
#include "stm32g0b1xx.h"
#include "io_module.h"
#include "fan_ctrl.h"
#include "eeprom_driver.h"
#include <stdbool.h>

/* ============================================================
 * CAN IDs — Commands (RX — this device receives)
 * ============================================================
 *
 *  CMD_FAN     [0x665] DLC=2
 *    Byte[0] : Fan mode      (0=OFF, 1=ON_MANUAL, 2=AUTO)
 *    Byte[1] : Duty cycle    (0–100 %)
 *
 *  CMD_HS      [0x666] DLC=5
 *    Byte[0] : RAIL_AUX      (0=disable, 1=enable)
 *    Byte[1] : RAIL_LED      (0=disable, 1=enable)
 *    Byte[2] : RAIL_DRIVE    (0=disable, 1=enable)
 *    Byte[3] : RAIL_CAP      (0=disable, 1=enable)
 *    Byte[4] : RAIL_SBC      (0=disable, 1=enable)
 *
 *  DEVICE_ADDR [0x667] DLC=2
 *    Byte[0] : 0xFF → system reset / bootloader trigger
 *
 *  CMD_OC      [0x668] DLC=4
 *    Byte[0] : Rail bitmask  (bit0=AUX … bit4=SBC | 0xFF=all rails)
 *    Byte[1] : Command       (0x01=set warning threshold, 0x02=reset fault)
 *    Byte[2] : Threshold high byte (mA, uint16_t big-endian)
 *    Byte[3] : Threshold low  byte (mA)
 *
 *  CMD_EEPROM  [0x669] DLC=1
 *    Byte[0] : 0x01=save config to EEPROM, 0x02=load default
 *
 *  CMD_UV      [0x66D] DLC=6
 *    Byte[0-1] : V24  UV threshold (uint16_t mV, big-endian)
 *    Byte[2-3] : VCAP UV threshold (uint16_t mV)
 *    Byte[4-5] : V12  UV threshold (uint16_t mV)
 * ============================================================ */
#define CMD_FAN             0x665
#define CMD_HS              0x666
#define DEVICE_ADDR         0x667
#define CMD_OC              0x668
#define CMD_EEPROM          0x669
#define CMD_UV              0x66D
#define CMD_CTRL            0x671

/* ============================================================
 * CAN IDs — Broadcast (TX — this device transmits)
 * ============================================================
 *
 *  BCAST_HS_STATE   [0x662] DLC=5
 *    Byte[0] : Enable bitmask     (1 bit per rail, LSB = RAIL_AUX)
 *    Byte[1] : Fault bitmask      (TPS2493 FLT pin asserted)
 *    Byte[2] : PGood bitmask      (TPS2493 PG  pin asserted)
 *    Byte[3] : OC Warning mask    (software threshold exceeded)
 *    Byte[4] : OC Triggered mask  (hardware FLT latched)
 *
 *  BCAST_HS_CURR_A  [0x663] DLC=8
 *    Byte[0-1] : BAT   current (uint16_t mA, big-endian)
 *    Byte[2-3] : CAP   current (uint16_t mA)
 *    Byte[4-5] : SBC   current (uint16_t mA)
 *    Byte[6-7] : DRIVE current (uint16_t mA)
 *
 *  BCAST_VOLTAGE    [0x664] DLC=8
 *    Byte[0-1] : V24  (uint16_t mV)
 *    Byte[2-3] : VCAP (uint16_t mV)
 *    Byte[4-5] : V12  (uint16_t mV)
 *    Byte[6]   : UV fault mask  (bit0=V24, bit1=VCAP, bit2=V12)
 *    Byte[7]   : Reserved
 *
 *  BCAST_FAN        [0x66A] DLC=4
 *    Byte[0]   : Fan mode     (0=OFF, 1=ON, 2=AUTO)
 *    Byte[1]   : Duty cycle   (0–100 %)
 *    Byte[2-3] : Temperature  (int16_t, °C × 10, e.g. 253 = 25.3 °C)
 *
 *  BCAST_EEPROM     [0x66B] DLC=8
 *    Byte[0]   : fan_default_mode
 *    Byte[1]   : fan_default_duty
 *    Byte[2]   : fan_min_duty
 *    Byte[3]   : fan_auto_on_temp
 *    Byte[4]   : fan_auto_off_temp
 *    Byte[5]   : hs_default_state  (bitmask)
 *    Byte[6-7] : Reserved
 *
 *  BCAST_HS_CURR_B  [0x66C] DLC=4
 *    Byte[0-1] : AUX current (uint16_t mA)
 *    Byte[2-3] : LED current (uint16_t mA)
 *
 *  BCAST_UV         [0x66E] DLC=6
 *    Byte[0-1] : V24  UV threshold (uint16_t mV)
 *    Byte[2-3] : VCAP UV threshold (uint16_t mV)
 *    Byte[4-5] : V12  UV threshold (uint16_t mV)
 *
 *  BCAST_OC_CFG_A   [0x66F] DLC=8   (OC threshold config echo)
 *    Byte[0-1] : OC threshold RAIL_AUX   (uint16_t mA)
 *    Byte[2-3] : OC threshold RAIL_LED   (uint16_t mA)
 *    Byte[4-5] : OC threshold RAIL_DRIVE (uint16_t mA)
 *    Byte[6-7] : OC threshold RAIL_CAP   (uint16_t mA)
 *
 *  BCAST_OC_CFG_B   [0x670] DLC=2   (OC threshold config echo cont.)
 *    Byte[0-1] : OC threshold RAIL_SBC   (uint16_t mA)
 * ============================================================ */
#define BCAST_HS_STATE      0x662
#define BCAST_HS_CURR_A     0x663
#define BCAST_VOLTAGE       0x664
#define BCAST_FAN           0x66A
#define BCAST_EEPROM        0x66B
#define BCAST_HS_CURR_B     0x66C
#define BCAST_UV            0x66E
#define BCAST_OC_CFG_A      0x66F
#define BCAST_OC_CFG_B      0x670
#define BCAST_IO_STATUS     0x672

/* ============================================================
 * Sub-command codes
 * ============================================================ */

/* CMD_OC Byte[1] */
#define OC_CMD_SET_THRESHOLD    0x01    // set per-rail software warning threshold
#define OC_CMD_RESET_FAULT      0x02    // clear latched hardware fault flags

/* CMD_EEPROM Byte[0] */
#define EEPROM_CMD_SAVE         0x01    // save current config to EEPROM
#define EEPROM_CMD_LOAD_DEFAULT 0x02    // restore factory defaults

/* UV fault bitmask bits (BCAST_VOLTAGE Byte[6]) */
#define UV_FAULT_V24            (1 << 0)
#define UV_FAULT_VCAP           (1 << 1)
#define UV_FAULT_V12            (1 << 2)

/* ============================================================
 * Structs
 * ============================================================ */

/* Parsed RX commands — written in ISR, consumed in main loop */
typedef struct {
    /* --- Fan --- */
    uint8_t     fan_mode;               // 0=OFF, 1=ON_MANUAL, 2=AUTO
    uint8_t     fan_duty;               // 0–100 %
    uint8_t     fan_cmd_received;       // set in ISR, clear after applying

    /* --- HS switch --- */
    uint8_t     hs_state[RAIL_COUNT];   // per-rail: 0=disable, 1=enable
    uint8_t     hs_cmd_received;

    /* --- Overcurrent --- */
    uint8_t     oc_rail_mask;           // rail bitmask target (0xFF = all)
    uint8_t     oc_cmd;                 // OC_CMD_SET_THRESHOLD / OC_CMD_RESET_FAULT
    uint16_t    oc_threshold_mA;        // warning threshold in mA
    uint8_t     oc_cmd_received;

    /* --- Undervoltage thresholds --- */
    uint16_t    uv_V24_mV;             // new V24  UV threshold
    uint16_t    uv_VCAP_mV;            // new VCAP UV threshold
    uint16_t    uv_V12_mV;             // new V12  UV threshold
    uint8_t     uv_cmd_received;

    /* --- GPIO / relay control --- */
    uint8_t     led_pwr_state;         // 0=OFF, 1=ON
    uint8_t     relay_state;           // 0=disable, 1=enable
    uint8_t     ctrl_cmd_received;

    /* --- EEPROM --- */
    uint8_t     flash_detected;
} CAN_RXMessage_t;

/* EEPROM action flags — set in ISR, cleared in main loop after action */
typedef struct {
    uint8_t write_eeprom_flag;          // 1 = save Config to EEPROM
    uint8_t reset_default_flag;         // 1 = call LoadDefault and re-save
} EEPROM_Command_t;

/* Overcurrent runtime state */
typedef struct {
    uint8_t     oc_warn_mask;                       // software OC warning bitmask
    uint8_t     oc_fault_mask;                      // hardware FLT bitmask (from HS_Fault)
    uint16_t    oc_threshold_mA[RAIL_COUNT];        // per-rail warning thresholds
} OC_Status_t;

/* Undervoltage runtime state */
typedef struct {
    uint8_t     uv_fault_mask;          // UV_FAULT_V24 | UV_FAULT_VCAP | UV_FAULT_V12
    uint16_t    uv_V24_mV;             // active UV threshold for V24
    uint16_t    uv_VCAP_mV;            // active UV threshold for VCAP
    uint16_t    uv_V12_mV;             // active UV threshold for V12
} UV_Status_t;

/* CAN bus transmission gating */
typedef struct {
    bool system_update_detected;
    bool system_transmit_stat;          // false = suppress TX (avoid bus conflicts)
    bool relay_enabled;                 // true = relay CAN1 <-> CAN2
} CAN_STATUS;

/* ============================================================
 * CAN2 Host Bus — relay / gateway interface
 * ============================================================
 *
 * Architecture:
 *   canHandle (FDCAN1, PB8/PB9) = Internal bus (bootloader + app primary)
 *   hfdcan2   (FDCAN2, PC2/PC3) = Host / external bus (relay gateway)
 *
 * Relay behaviour (no filter):
 *   - CAN2 RX → process commands + relay raw frame to CAN1
 *   - CAN1 RX → process commands + relay raw frame to CAN2
 *   - All periodic broadcasts → sent on BOTH CAN1 and CAN2
 * ============================================================ */

/* ============================================================
 * Extern declarations
 * ============================================================ */
extern CAN_RXMessage_t      can_rxMessage;
extern EEPROM_Command_t     eeprom_cmd;
extern OC_Status_t          oc_status;
extern UV_Status_t          uv_status;
extern CAN_STATUS           canstat;

/* canHandle is a #define alias for hfdcan1 (see main.h) */
extern FDCAN_HandleTypeDef   hfdcan1;            /* FDCAN1 — internal bus  */
extern FDCAN_HandleTypeDef   hfdcan2;            /* FDCAN2 — host bus      */
extern FDCAN_TxHeaderTypeDef TxHeader;

/* ============================================================
 * Function prototypes
 * ============================================================ */

/* Init — call in PS_App_Init() */
void CANInitTxHeader(void);

/* Init CAN2 host bus — call in State_Init() after CAN1 is ready */
void CAN2_Host_Init(void);

/* Send on internal bus only (CAN1 / canHandle) */
HAL_StatusTypeDef CAN_Send(uint16_t canid, uint8_t dlc, uint8_t *data);

/* Send on host bus only (CAN2 / hfdcan2) */
HAL_StatusTypeDef CAN2_Send(uint16_t canid, uint8_t dlc, uint8_t *data);

/* Send on BOTH internal + host bus (used by all broadcasts) */
HAL_StatusTypeDef CAN_SendAll(uint16_t canid, uint8_t dlc, uint8_t *data);

/* RX callback — overrides HAL weak symbol (handles both FDCAN1 + FDCAN2) */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);

/* Flash-over-CAN watchdog (call periodically in main loop) */
void FOCdetection(void);

/* ---- Broadcast functions (all dual-send on CAN1 + CAN2) ---- */

/* [0x662] enable / fault / pgood / OC-warn / OC-fault bitmasks */
void CAN_Broadcast_HS_State(void);

/* [0x663] BAT, CAP, SBC, DRIVE current (uint16_t mA) */
void CAN_Broadcast_HS_Current_A(SystemMeasurement_t *ms);

/* [0x66C] AUX, LED current (uint16_t mA) */
void CAN_Broadcast_HS_Current_B(SystemMeasurement_t *ms);

/* [0x664] V24, VCAP, V12 in mV + UV fault bitmask */
void CAN_Broadcast_Voltage(SystemMeasurement_t *ms);

/* [0x66A] fan mode, duty %, temperature (int16_t °C×10) */
void CAN_Broadcast_Fan(FanCTRL_t *fan, float temp_C);

/* [0x66B] fan defaults + hs_default_state from Config */
void CAN_Broadcast_EEPROM(Config *cfg);

/* [0x66E] UV thresholds from active uv_status */
void CAN_Broadcast_UV(void);

/* [0x66F + 0x670] OC thresholds per rail from oc_status */
void CAN_Broadcast_OC_Config(void);

/* [0x672] SW pin state, V_LED_PWR state, relay status */
void CAN_Broadcast_IO_Status(void);

#endif /* INC_CAN_OPERATION_H_ */
