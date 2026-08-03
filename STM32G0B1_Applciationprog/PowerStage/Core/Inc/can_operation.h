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
 * CAN IDs — PowerStage sub-block 0x130..0x15F (CANopen-safe gap)
 *
 * 0x130 / 0x131 reserved for OpenBLT bootloader RX/TX. The bootloader
 * RX (DEVICE_ADDR, 0x130) doubles as the application's "enter bootloader"
 * trigger: a single XCP CONNECT (byte[0]=0xFF, dlc=2) on 0x130 resets
 * the running app and is then re-handled by the bootloader on the same ID.
 * Must match STM32G0B1_Bootloader/G0B1_PowerStage_Boot/App/blt_conf.h.
 * ============================================================
 *
 *  CMD_FAN     [0x140] DLC=2
 *    Byte[0] : Fan mode      (0=OFF, 1=ON_MANUAL, 2=AUTO)
 *    Byte[1] : Duty cycle    (0–100 %)
 *
 *  CMD_HS      [0x141] DLC=5
 *    Byte[0] : RAIL_AUX      (0=disable, 1=enable)
 *    Byte[1] : RAIL_LED      (0=disable, 1=enable)
 *    Byte[2] : RAIL_DRIVE    (0=disable, 1=enable)
 *    Byte[3] : RAIL_CAP      (0=disable, 1=enable)
 *    Byte[4] : RAIL_SBC      (IGNORED — no MCU-controlled EN line on
 *                             this PowerStage board; SBC is permanently
 *                             enabled by hardware. The byte is reserved
 *                             for backward compatibility.)
 *
 *  DEVICE_ADDR [0x130] DLC=2
 *    Byte[0] : 0xFF → system reset into bootloader
 *
 *  CMD_OC      [0x142] DLC=8   (mirrors BCAST_OC_CFG_A byte-for-byte)
 *    Byte[0-1] : RAIL_AUX   OC warning threshold (uint16_t mA, big-endian)
 *    Byte[2-3] : RAIL_LED   OC warning threshold (uint16_t mA)
 *    Byte[4-5] : RAIL_DRIVE OC warning threshold (uint16_t mA)
 *    Byte[6-7] : RAIL_CAP   OC warning threshold (uint16_t mA)
 *    RAIL_SBC has no MCU-driven EN line — no software OC, not in this frame.
 *    Set a rail's threshold to 0 to disable software OC on that rail.
 *    Persist with CMD_EEPROM 0x01.
 *
 *  CMD_EEPROM  [0x143] DLC=1
 *    Byte[0] : 0x01=save config to EEPROM, 0x02=load default
 *
 *  CMD_UV      [0x144] DLC=6
 *    Byte[0-1] : V24  UV threshold (uint16_t mV, big-endian)
 *    Byte[2-3] : VCAP UV threshold (uint16_t mV)
 *    Byte[4-5] : V12  UV threshold (uint16_t mV)
 *
 *  CMD_CTRL    [0x145] DLC=2
 *    Byte[0] : V_LED_PWR     (0=OFF, 1=ON)
 *    Byte[1] : CAN relay     (0=disable, 1=enable)
 *
 *  CMD_PAGE_DWELL [0x146] DLC=3
 *    Byte[0] : Page 0 (OVERVIEW)     dwell in 500 ms ticks. 0=default.
 *    Byte[1] : Page 1 (RAIL_STATUS)  dwell in 500 ms ticks. 0=default.
 *    Byte[2] : Page 2 (FAULT_DETAIL) dwell in 500 ms ticks. 0=default.
 *    Range 1..255 → 0.5 s .. 127.5 s per page. Send CMD_EEPROM 0x01
 *    afterward to persist across reboots.
 *
 *  CMD_BAT_CFG [0x147] DLC=1
 *    Byte[0] : Battery SOC-low warning threshold in % (0..100).
 *              0    = warning disabled.
 *              1-100 = trip when filtered SOC < threshold.
 *              When tripped, UV_Fault_Mask bit 3 (UV_FAULT_SOC_LOW) is set
 *              in BCAST_VOLTAGE and the OLED V24/SOC row blinks at 1 Hz.
 *              Send CMD_EEPROM 0x01 afterward to persist.
 *
 *  CMD_OC_RESET [0x148] DLC=1
 *    Byte[0] : Rail bitmask (bit0=AUX, bit1=LED, bit2=DRIVE, bit3=CAP).
 *              For each set bit, clear the latched software OC warn flag
 *              and re-enable the rail. 0x0F = all controllable rails.
 * ============================================================ */
#define DEVICE_ADDR         0x130
#define CMD_FAN             0x140
#define CMD_HS              0x141
#define CMD_OC              0x142
#define CMD_EEPROM          0x143
#define CMD_UV              0x144
#define CMD_CTRL            0x145
#define CMD_PAGE_DWELL      0x146
#define CMD_BAT_CFG         0x147
#define CMD_OC_RESET        0x148

/* ============================================================
 * CAN IDs — Broadcast (TX — this device transmits, 0x150..0x15F)
 * ============================================================
 *
 *  BCAST_HS_STATE   [0x150] DLC=5
 *    Byte[0] : Enable bitmask     (1 bit per rail, LSB = RAIL_AUX)
 *    Byte[1] : Fault bitmask      (TPS2493 FLT pin asserted)
 *    Byte[2] : PGood bitmask      (TPS2493 PG  pin asserted)
 *    Byte[3] : OC Warning mask    (software threshold exceeded)
 *    Byte[4] : OC Triggered mask  (hardware FLT latched)
 *
 *  BCAST_HS_CURR_A  [0x151] DLC=8
 *    Byte[0-1] : BAT   current (uint16_t mA, big-endian)
 *    Byte[2-3] : CAP   current (uint16_t mA)
 *    Byte[4-5] : SBC   current (uint16_t mA)
 *    Byte[6-7] : DRIVE current (uint16_t mA)
 *
 *  BCAST_VOLTAGE    [0x152] DLC=8
 *    Byte[0-1] : V24  (uint16_t mV)
 *    Byte[2-3] : VCAP (uint16_t mV)
 *    Byte[4-5] : V12  (uint16_t mV)
 *    Byte[6]   : UV fault mask  (bit0=V24, bit1=VCAP, bit2=V12)
 *    Byte[7]   : Reserved
 *
 *  BCAST_FAN        [0x153] DLC=4
 *    Byte[0]   : Fan mode     (0=OFF, 1=ON, 2=AUTO)
 *    Byte[1]   : Duty cycle   (0–100 %)
 *    Byte[2-3] : Temperature  (int16_t, °C × 10, e.g. 253 = 25.3 °C)
 *
 *  BCAST_EEPROM     [0x154] DLC=8
 *    Byte[0]   : fan_default_mode
 *    Byte[1]   : fan_default_duty
 *    Byte[2]   : fan_min_duty
 *    Byte[3]   : fan_auto_on_temp
 *    Byte[4]   : fan_auto_off_temp
 *    Byte[5]   : hs_default_state  (bitmask)
 *    Byte[6-7] : Reserved
 *
 *  BCAST_HS_CURR_B  [0x155] DLC=4
 *    Byte[0-1] : AUX current (uint16_t mA)
 *    Byte[2-3] : LED current (uint16_t mA)
 *
 *  BCAST_UV         [0x156] DLC=6
 *    Byte[0-1] : V24  UV threshold (uint16_t mV)
 *    Byte[2-3] : VCAP UV threshold (uint16_t mV)
 *    Byte[4-5] : V12  UV threshold (uint16_t mV)
 *
 *  BCAST_OC_CFG_A   [0x157] DLC=8   (OC threshold config echo — mirrors CMD_OC)
 *    Byte[0-1] : OC threshold RAIL_AUX   (uint16_t mA)
 *    Byte[2-3] : OC threshold RAIL_LED   (uint16_t mA)
 *    Byte[4-5] : OC threshold RAIL_DRIVE (uint16_t mA)
 *    Byte[6-7] : OC threshold RAIL_CAP   (uint16_t mA)
 *
 *  BCAST_IO_STATUS  [0x159] DLC=3
 *    Byte[0]   : SW pin       (0/1)
 *    Byte[1]   : V_LED_PWR    (0/1)
 *    Byte[2]   : CAN relay    (0=disabled, 1=enabled)
 *
 *  BCAST_BATTERY_CFG[0x15A] DLC=8   (battery static config — for host UI)
 *    Byte[0-1] : V_cutoff_mV  (uint16_t mV, big-endian)  — 0% SOC reference
 *    Byte[2-3] : V_full_mV    (uint16_t mV, big-endian)  — 100% SOC reference
 *    Byte[4-5] : R_int_mOhm   (uint16_t mΩ, big-endian)  — pack internal resistance used for IR comp
 *    Byte[6]   : Cell_Count   (uint8_t)                  — series count (6 for 6S)
 *    Byte[7]   : Reserved
 * ============================================================ */
#define BCAST_HS_STATE      0x150
#define BCAST_HS_CURR_A     0x151
#define BCAST_VOLTAGE       0x152
#define BCAST_FAN           0x153
#define BCAST_EEPROM        0x154
#define BCAST_HS_CURR_B     0x155
#define BCAST_UV            0x156
#define BCAST_OC_CFG_A      0x157
#define BCAST_IO_STATUS     0x159
#define BCAST_BATTERY_CFG   0x15A

/* ============================================================
 * Sub-command codes
 * ============================================================ */

/* CMD_EEPROM Byte[0] */
#define EEPROM_CMD_SAVE         0x01    // save current config to EEPROM
#define EEPROM_CMD_LOAD_DEFAULT 0x02    // restore factory defaults

/* UV fault bitmask bits (BCAST_VOLTAGE Byte[6]) */
#define UV_FAULT_V24            (1 << 0)
#define UV_FAULT_VCAP           (1 << 1)
#define UV_FAULT_V12            (1 << 2)
#define UV_FAULT_SOC_LOW        (1 << 3)   /* Battery SOC < BATTERY_LOW_SOC_PCT — see battery.h */

/* ============================================================
 * Structs
 * ============================================================ */

/* Parsed RX commands — written in ISR, consumed in main loop */
typedef struct {
    /* --- Fan --- */
    uint8_t     fan_mode;               // 0=OFF, 1=ON_MANUAL, 2=AUTO
    uint8_t     fan_duty;               // 0–100 %
    uint8_t     fan_cmd_received;       // set in ISR, clear after applying

    /* AUTO tuning, carried only by the DLC=5 form of CMD_FAN. Before this the
     * thresholds could only be changed by reflashing the EEPROM defaults.
     * fan_cfg_valid is 0 for a short frame, and also for a rejected
     * (on <= off) hysteresis pair, so the applier can tell "not supplied"
     * from "supplied and usable". */
    uint8_t     fan_min_duty;           // 0–100 %, floor for a *running* fan
    uint8_t     fan_auto_on_temp;       // °C, AUTO turns on at or above this
    uint8_t     fan_auto_off_temp;      // °C, AUTO turns off below this
    uint8_t     fan_cfg_valid;          // 1 = the three fields above are set

    /* --- HS switch --- */
    uint8_t     hs_state[RAIL_COUNT];   // per-rail: 0=disable, 1=enable
    uint8_t     hs_cmd_received;

    /* --- Overcurrent thresholds (CMD_OC) --- */
    uint16_t    oc_threshold_mA[RAIL_COUNT];  // received thresholds; SBC slot unused
    uint8_t     oc_cmd_received;

    /* --- Overcurrent fault reset (CMD_OC_RESET) --- */
    uint8_t     oc_reset_mask;          // rail bitmask to clear (bit0=AUX..bit3=CAP)
    uint8_t     oc_reset_received;

    /* --- Undervoltage thresholds --- */
    uint16_t    uv_V24_mV;             // new V24  UV threshold
    uint16_t    uv_VCAP_mV;            // new VCAP UV threshold
    uint16_t    uv_V12_mV;             // new V12  UV threshold
    uint8_t     uv_cmd_received;

    /* --- GPIO / relay control --- */
    uint8_t     led_pwr_state;         // 0=OFF, 1=ON
    uint8_t     relay_state;           // 0=disable, 1=enable
    uint8_t     ctrl_cmd_received;

    /* --- OLED per-page dwell (in 500 ms scheduler ticks) --- */
    uint8_t     page_dwell[CONFIG_PAGE_COUNT];
    uint8_t     page_dwell_cmd_received;

    /* --- Battery SOC-low warning threshold (% SOC, 0 = disabled) --- */
    uint8_t     bat_low_soc_pct;
    uint8_t     bat_cfg_cmd_received;

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

/* Bus-Off recovery limits */
#define CAN_BUSOFF_MAX_RECOVERIES   5       // max recoveries within window
#define CAN_BUSOFF_WINDOW_MS        10000   // rolling window (ms)

/* CAN bus transmission gating */
typedef struct {
    bool system_update_detected;
    bool system_transmit_stat;          // false = suppress TX (avoid bus conflicts)
    bool relay_enabled;                 // true = relay CAN1 <-> CAN2

    /* Bus-Off health tracking (per bus) */
    uint8_t  can1_busoff_count;         // recovery attempts within window
    uint32_t can1_busoff_first_tick;    // tick of first recovery in current window
    bool     can1_bus_ok;               // false = Bus_Off recovery limit hit

    uint8_t  can2_busoff_count;
    uint32_t can2_busoff_first_tick;
    bool     can2_bus_ok;
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

/* Error callback — Bus_Off auto-recovery (overrides HAL weak symbol) */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs);

/* Flash-over-CAN watchdog (call periodically in main loop) */
void FOCdetection(void);

/* ---- Broadcast functions (all dual-send on CAN1 + CAN2) ---- */

/* [0x150] enable / fault / pgood / OC-warn / OC-fault bitmasks */
void CAN_Broadcast_HS_State(void);

/* [0x151] BAT, CAP, SBC, DRIVE current (uint16_t mA) */
void CAN_Broadcast_HS_Current_A(SystemMeasurement_t *ms);

/* [0x155] AUX, LED current (uint16_t mA) */
void CAN_Broadcast_HS_Current_B(SystemMeasurement_t *ms);

/* [0x152] V24, VCAP, V12 in mV + UV fault bitmask */
void CAN_Broadcast_Voltage(SystemMeasurement_t *ms);

/* [0x153] fan mode, duty %, temperature (int16_t °C×10) */
void CAN_Broadcast_Fan(FanCTRL_t *fan, float temp_C);

/* [0x154] fan defaults + hs_default_state from Config */
void CAN_Broadcast_EEPROM(Config *cfg);

/* [0x156] UV thresholds from active uv_status */
void CAN_Broadcast_UV(void);

/* [0x157] OC thresholds per rail from oc_status */
void CAN_Broadcast_OC_Config(void);

/* [0x159] SW pin state, V_LED_PWR state, relay status */
void CAN_Broadcast_IO_Status(void);

/* [0x15A] static battery config (V_cutoff, V_full, R_int, cell count) */
void CAN_Broadcast_Battery_Cfg(void);

#endif /* INC_CAN_OPERATION_H_ */
