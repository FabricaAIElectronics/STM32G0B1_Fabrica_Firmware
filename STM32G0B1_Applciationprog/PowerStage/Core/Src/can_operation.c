/*
 * can_operation.c
 *
 *  Created on: Oct 31, 2025
 *      Author: jordan
 *
 *  CAN bus architecture:
 *    canHandle (FDCAN1, PB8/PB9) — Internal bus (OpenBLT bootloader + app primary)
 *    hfdcan2   (FDCAN2, PC2/PC3) — Host / external bus (relay gateway)
 *
 *  Relay behaviour (no filter):
 *    - Any frame received on CAN2 → parsed for commands + relayed raw to CAN1
 *    - Any frame received on CAN1 (not from self) → relayed raw to CAN2
 *    - All periodic broadcasts → sent on BOTH buses via CAN_SendAll()
 */

#include "can_operation.h"
#include "main.h"
#include "io_module.h"
#include "fan_ctrl.h"
#include "eeprom_driver.h"
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * Private variables
 * ============================================================ */
static volatile uint32_t     lastFlashMsgTick = 0;

/* CAN2 host bus TX header (separate from CAN1's TxHeader) */
static FDCAN_TxHeaderTypeDef TxHeader2;

/* ============================================================
 * Exported variables
 * ============================================================ */
FDCAN_TxHeaderTypeDef TxHeader;

CAN_STATUS canstat = {
    .system_update_detected = false,
    .system_transmit_stat   = true,         // TX allowed on startup
    .relay_enabled          = true,         // CAN1<>CAN2 relay on by default
    .can1_busoff_count      = 0,
    .can1_busoff_first_tick = 0,
    .can1_bus_ok            = true,
    .can2_busoff_count      = 0,
    .can2_busoff_first_tick = 0,
    .can2_bus_ok            = true
};

CAN_RXMessage_t  can_rxMessage = {0};
EEPROM_Command_t eeprom_cmd    = {0};

OC_Status_t oc_status = {
    .oc_warn_mask        = 0,
    .oc_fault_mask       = 0,
    .oc_threshold_mA     = {0}             // 0 = disabled until Config loaded
};

UV_Status_t uv_status = {
    .uv_fault_mask  = 0,
    .uv_V24_mV      = 20000,              // 20.0 V default
    .uv_VCAP_mV     = 20000,
    .uv_V12_mV      = 10000              // 10.0 V default
};

/* ============================================================
 * CANInitTxHeader — CAN1 (internal bus) TX header
 * ============================================================ */
void CANInitTxHeader(void)
{
    TxHeader.IdType              = FDCAN_STANDARD_ID;
    TxHeader.Identifier          = 0x100;
    TxHeader.DataLength          = FDCAN_DLC_BYTES_8;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    TxHeader.BitRateSwitch       = FDCAN_BRS_OFF;
    TxHeader.FDFormat            = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker       = 0;

    /* Accept all standard frames — matches CAN2 filter config */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_ACCEPT_IN_RX_FIFO0,   /* non-matching std */
                                 FDCAN_ACCEPT_IN_RX_FIFO0,   /* non-matching ext */
                                 FDCAN_FILTER_REMOTE,         /* reject remote std */
                                 FDCAN_FILTER_REMOTE);        /* reject remote ext */

    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);

    /* Enable error interrupts for Bus_Off recovery */
    HAL_FDCAN_ActivateNotification(&hfdcan1,
        FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING, 0);

    HAL_FDCAN_Start(&hfdcan1);

    canstat.can1_bus_ok = true;
    canstat.can1_busoff_count = 0;
}

/* ============================================================
 * CAN2_Host_Init — start FDCAN2 host bus with RX notification
 * Call after MX_FDCAN2_Init() and after CAN1 is ready.
 * ============================================================ */
void CAN2_Host_Init(void)
{
    /* Initialise CAN2 TX header (mirrors CAN1 defaults) */
    TxHeader2.IdType              = FDCAN_STANDARD_ID;
    TxHeader2.Identifier          = 0x100;
    TxHeader2.DataLength          = FDCAN_DLC_BYTES_8;
    TxHeader2.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    TxHeader2.BitRateSwitch       = FDCAN_BRS_OFF;
    TxHeader2.FDFormat            = FDCAN_CLASSIC_CAN;
    TxHeader2.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    TxHeader2.MessageMarker       = 0;

    /* Accept all frames — no filter (reject nothing) */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan2,
                                 FDCAN_ACCEPT_IN_RX_FIFO0,   /* non-matching std */
                                 FDCAN_ACCEPT_IN_RX_FIFO0,   /* non-matching ext */
                                 FDCAN_FILTER_REMOTE,         /* reject remote std */
                                 FDCAN_FILTER_REMOTE);        /* reject remote ext */

    /* Enable RX FIFO0 new-message interrupt for CAN2 */
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);

    /* Enable error interrupts for Bus_Off recovery */
    HAL_FDCAN_ActivateNotification(&hfdcan2,
        FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING, 0);

    /* Start CAN2 peripheral */
    HAL_FDCAN_Start(&hfdcan2);

    canstat.can2_bus_ok = true;
    canstat.can2_busoff_count = 0;
}

/* ============================================================
 * CAN_Send — transmit on CAN1 (internal bus) only
 * ============================================================ */
HAL_StatusTypeDef CAN_Send(uint16_t canid, uint8_t dlc, uint8_t *data)
{
    if (!canstat.can1_bus_ok) return HAL_ERROR;     // Bus_Off — skip TX

    uint32_t timeout = HAL_GetTick() + 10;

    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0) {
        if (HAL_GetTick() > timeout) return HAL_ERROR;
    }

    TxHeader.Identifier = canid;
    TxHeader.DataLength = dlc;

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, data);
}

/* ============================================================
 * CAN2_Send — transmit on CAN2 (host bus) only
 * ============================================================ */
HAL_StatusTypeDef CAN2_Send(uint16_t canid, uint8_t dlc, uint8_t *data)
{
    if (!canstat.can2_bus_ok) return HAL_ERROR;     // Bus_Off — skip TX

    uint32_t timeout = HAL_GetTick() + 10;

    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) == 0) {
        if (HAL_GetTick() > timeout) return HAL_ERROR;
    }

    TxHeader2.Identifier = canid;
    TxHeader2.DataLength = dlc;

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &TxHeader2, data);
}

/* ============================================================
 * CAN_SendAll — transmit on BOTH CAN1 + CAN2
 * Used by all broadcast functions for dual-bus operation.
 * ============================================================ */
HAL_StatusTypeDef CAN_SendAll(uint16_t canid, uint8_t dlc, uint8_t *data)
{
    HAL_StatusTypeDef r1 = CAN_Send(canid, dlc, data);
    HAL_StatusTypeDef r2 = CAN2_Send(canid, dlc, data);
    return (r1 == HAL_OK && r2 == HAL_OK) ? HAL_OK : HAL_ERROR;
}

/* ============================================================
 * Parse_RX_Commands — process command payload (shared by both buses)
 * ============================================================ */
static void Parse_RX_Commands(uint32_t id, uint8_t *data, uint32_t dlc)
{
    /* ---- System reset / bootloader ---- */
    if (id == DEVICE_ADDR) {
        if ((data[0] == 0xFF) && (dlc == FDCAN_DLC_BYTES_2)) {
            canstat.system_update_detected = true;
            canstat.system_transmit_stat   = false;
            NVIC_SystemReset();
        }
    }

    /* ---- Fan control ---- */
    if (id == CMD_FAN) {
        uint8_t mode = data[0];
        uint8_t duty = data[1];
        if (mode > FAN_ON_AUTO) mode = FAN_ON_AUTO;
        if (duty > 100)         duty = 100;

        can_rxMessage.fan_mode         = mode;
        can_rxMessage.fan_duty         = duty;
        can_rxMessage.fan_cmd_received = 1;
    }

    /* ---- HS switch control ---- */
    if (id == CMD_HS) {
        for (uint8_t i = 0; i < RAIL_COUNT; i++) {
            can_rxMessage.hs_state[i] = (data[i] != 0) ? 1 : 0;
        }
        can_rxMessage.hs_cmd_received = 1;
    }

    /* ---- Overcurrent threshold / fault reset ---- */
    if (id == CMD_OC) {
        can_rxMessage.oc_rail_mask     = data[0];
        can_rxMessage.oc_cmd          = data[1];
        can_rxMessage.oc_threshold_mA = ((uint16_t)data[2] << 8) | data[3];
        can_rxMessage.oc_cmd_received = 1;
    }

    /* ---- EEPROM save / load default ---- */
    if (id == CMD_EEPROM) {
        if (data[0] & EEPROM_CMD_SAVE)         eeprom_cmd.write_eeprom_flag  = 1;
        if (data[0] & EEPROM_CMD_LOAD_DEFAULT)  eeprom_cmd.reset_default_flag = 1;
    }

    /* ---- Undervoltage thresholds ---- */
    if (id == CMD_UV) {
        can_rxMessage.uv_V24_mV       = ((uint16_t)data[0] << 8) | data[1];
        can_rxMessage.uv_VCAP_mV      = ((uint16_t)data[2] << 8) | data[3];
        can_rxMessage.uv_V12_mV       = ((uint16_t)data[4] << 8) | data[5];
        can_rxMessage.uv_cmd_received = 1;
    }

    /* ---- GPIO / relay control ---- */
    if (id == CMD_CTRL) {
        can_rxMessage.led_pwr_state     = data[0];
        can_rxMessage.relay_state       = data[1];
        can_rxMessage.ctrl_cmd_received = 1;
    }

    /* ---- Flash-over-CAN watchdog ---- */
    if ((data[0] == 0xFF) && (dlc == FDCAN_DLC_BYTES_1)) {
        can_rxMessage.flash_detected = 1;
        lastFlashMsgTick = HAL_GetTick();
    }
}

/* ============================================================
 * HAL_FDCAN_RxFifo0Callback
 * Overrides HAL weak symbol — called for BOTH FDCAN1 and FDCAN2.
 *
 * Behaviour:
 *   CAN1 (canHandle) RX → parse commands + relay raw to CAN2
 *   CAN2 (hfdcan2)   RX → parse commands + relay raw to CAN1
 * ============================================================ */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if (!(RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE)) return;

    /* Use stack-local variables to avoid shared-state races between buses */
    FDCAN_RxHeaderTypeDef rxHdr;
    uint8_t rxBuf[8] = {0};

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHdr, rxBuf) != HAL_OK)
        return;

    /* Parse commands from whichever bus the frame arrived on */
    Parse_RX_Commands(rxHdr.Identifier, rxBuf, rxHdr.DataLength);

    /* ---- Relay: non-blocking forward to the OTHER bus ----
     * Previous implementation called CAN_Send()/CAN2_Send() which use a
     * busy-wait loop with HAL_GetTick() timeout.  Inside this ISR, SysTick
     * cannot increment, so the timeout never fires → infinite loop → freeze.
     *
     * Fix: check TX FIFO once; if full, drop the relay frame silently.
     * A stack-local TX header avoids races with the main-loop TxHeader. */
    if (!canstat.relay_enabled) return;

    FDCAN_TxHeaderTypeDef relayHdr = {
        .IdType              = FDCAN_STANDARD_ID,
        .Identifier          = rxHdr.Identifier,
        .DataLength          = rxHdr.DataLength,
        .ErrorStateIndicator = FDCAN_ESI_PASSIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0
    };

    if (hfdcan->Instance == FDCAN1) {
        /* CAN1 RX → relay to CAN2 (host bus) */
        if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) > 0)
            HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &relayHdr, rxBuf);
    }
    else if (hfdcan->Instance == FDCAN2) {
        /* CAN2 RX → relay to CAN1 (internal bus) */
        if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) > 0)
            HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &relayHdr, rxBuf);
    }
}

/* ============================================================
 * HAL_FDCAN_ErrorStatusCallback
 * Overrides HAL weak symbol — called on Bus_Off, Error_Passive,
 * and Error_Warning status changes for BOTH FDCAN1 and FDCAN2.
 *
 * On Bus_Off: stop → restart the peripheral and re-enable
 * notifications.  A rolling window limits recovery attempts
 * (CAN_BUSOFF_MAX_RECOVERIES within CAN_BUSOFF_WINDOW_MS).
 * If the limit is hit the bus is marked permanently faulted
 * until the next power cycle / NRST.
 * ============================================================ */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan,
                                   uint32_t ErrorStatusITs)
{
    if (!(ErrorStatusITs & FDCAN_IT_BUS_OFF))
        return;

    uint32_t now = HAL_GetTick();

    /* Identify which bus entered Bus_Off */
    uint8_t  *count;
    uint32_t *first_tick;
    bool     *bus_ok;

    if (hfdcan->Instance == FDCAN1) {
        count      = &canstat.can1_busoff_count;
        first_tick = &canstat.can1_busoff_first_tick;
        bus_ok     = &canstat.can1_bus_ok;
    } else {
        count      = &canstat.can2_busoff_count;
        first_tick = &canstat.can2_busoff_first_tick;
        bus_ok     = &canstat.can2_bus_ok;
    }

    /* Reset rolling window if expired */
    if ((now - *first_tick) > CAN_BUSOFF_WINDOW_MS) {
        *count      = 0;
        *first_tick = now;
    }

    (*count)++;

    if (*count > CAN_BUSOFF_MAX_RECOVERIES) {
        /* Too many Bus_Off events — mark bus permanently faulted */
        *bus_ok = false;
        HAL_FDCAN_Stop(hfdcan);
        return;
    }

    /* Attempt recovery: stop → start → re-enable notifications */
    HAL_FDCAN_Stop(hfdcan);
    HAL_FDCAN_Start(hfdcan);

    HAL_FDCAN_ActivateNotification(hfdcan,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    HAL_FDCAN_ActivateNotification(hfdcan,
        FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING, 0);
}

/* ============================================================
 * FOCdetection — call periodically in main loop
 * Clears flash_detected if no 0xFF frame for 500 ms
 * ============================================================ */
void FOCdetection(void)
{
    if (HAL_GetTick() - lastFlashMsgTick > 500) {
        can_rxMessage.flash_detected = 0;
    }
}

/* ============================================================
 * CAN_Broadcast_HS_State
 *
 * [0x662] DLC=5
 *   Byte[0]: Enable bitmask
 *   Byte[1]: Fault  bitmask   (TPS2493 FLT pin)
 *   Byte[2]: PGood  bitmask   (TPS2493 PG  pin)
 *   Byte[3]: OC Warning mask  (software threshold exceeded)
 *   Byte[4]: OC Triggered mask(hardware FLT latched)
 * ============================================================ */
void CAN_Broadcast_HS_State(void)
{
    if (!canstat.system_transmit_stat) return;

    uint8_t enable_mask = 0;
    uint8_t fault_mask  = 0;
    uint8_t pgood_mask  = 0;

    for (uint8_t i = 0; i < RAIL_COUNT; i++) {
        if (HS_PGood(&hotswap[i])) { pgood_mask  |= (1u << i); enable_mask |= (1u << i); }
        if (HS_Fault(&hotswap[i]))   fault_mask  |= (1u << i);
    }

    oc_status.oc_fault_mask = fault_mask;   // keep OC fault in sync with HW FLT pins

    uint8_t data[5];
    data[0] = enable_mask;
    data[1] = fault_mask;
    data[2] = pgood_mask;
    data[3] = oc_status.oc_warn_mask;
    data[4] = oc_status.oc_fault_mask;

    CAN_SendAll(BCAST_HS_STATE, FDCAN_DLC_BYTES_5, data);
}

/* ============================================================
 * CAN_Broadcast_HS_Current_A
 *
 * [0x663] DLC=8 — BAT, CAP, SBC, DRIVE current (uint16_t mA)
 * ============================================================ */
void CAN_Broadcast_HS_Current_A(SystemMeasurement_t *ms)
{
    if (!canstat.system_transmit_stat) return;

    uint16_t bat   = ms->current_mA._currbat;
    uint16_t cap   = ms->current_mA._currcap;
    uint16_t sbc   = ms->current_mA._currsbc;
    uint16_t drive = ms->current_mA._currdrive;

    uint8_t data[8];
    data[0] = (bat   >> 8) & 0xFF;  data[1] = bat   & 0xFF;
    data[2] = (cap   >> 8) & 0xFF;  data[3] = cap   & 0xFF;
    data[4] = (sbc   >> 8) & 0xFF;  data[5] = sbc   & 0xFF;
    data[6] = (drive >> 8) & 0xFF;  data[7] = drive & 0xFF;

    CAN_SendAll(BCAST_HS_CURR_A, FDCAN_DLC_BYTES_8, data);
}

/* ============================================================
 * CAN_Broadcast_HS_Current_B
 *
 * [0x66C] DLC=4 — AUX, LED current (uint16_t mA)
 * ============================================================ */
void CAN_Broadcast_HS_Current_B(SystemMeasurement_t *ms)
{
    if (!canstat.system_transmit_stat) return;

    uint16_t aux = ms->current_mA._curraux;
    uint16_t led = ms->current_mA._currled;

    uint8_t data[4];
    data[0] = (aux >> 8) & 0xFF;  data[1] = aux & 0xFF;
    data[2] = (led >> 8) & 0xFF;  data[3] = led & 0xFF;

    CAN_SendAll(BCAST_HS_CURR_B, FDCAN_DLC_BYTES_4, data);
}

/* ============================================================
 * CAN_Broadcast_Voltage
 *
 * [0x664] DLC=8
 *   Byte[0-1]: V24  (uint16_t mV)
 *   Byte[2-3]: VCAP (uint16_t mV)
 *   Byte[4-5]: V12  (uint16_t mV)
 *   Byte[6]  : UV fault mask  (UV_FAULT_V24 | UV_FAULT_VCAP | UV_FAULT_V12)
 *   Byte[7]  : Reserved
 *
 * Also updates uv_status.uv_fault_mask based on live measurements.
 * ============================================================ */
void CAN_Broadcast_Voltage(SystemMeasurement_t *ms)
{
    if (!canstat.system_transmit_stat) return;

    uint16_t v24  = ms->voltage_mV.V24;
    uint16_t vcap = ms->voltage_mV.VCAP;
    uint16_t v12  = ms->voltage_mV.V12;

    /* Update UV fault flags from live measurements vs thresholds */
    uv_status.uv_fault_mask = 0;
    if (v24  < uv_status.uv_V24_mV)  uv_status.uv_fault_mask |= UV_FAULT_V24;
    if (vcap < uv_status.uv_VCAP_mV) uv_status.uv_fault_mask |= UV_FAULT_VCAP;
    if (v12  < uv_status.uv_V12_mV)  uv_status.uv_fault_mask |= UV_FAULT_V12;

    uint8_t data[8];
    data[0] = (v24  >> 8) & 0xFF;  data[1] = v24  & 0xFF;
    data[2] = (vcap >> 8) & 0xFF;  data[3] = vcap & 0xFF;
    data[4] = (v12  >> 8) & 0xFF;  data[5] = v12  & 0xFF;
    data[6] = uv_status.uv_fault_mask;
    data[7] = 0x00;

    CAN_SendAll(BCAST_VOLTAGE, FDCAN_DLC_BYTES_8, data);
}

/* ============================================================
 * CAN_Broadcast_Fan
 *
 * [0x66A] DLC=4
 *   Byte[0]  : Fan mode  (0=OFF, 1=ON, 2=AUTO)
 *   Byte[1]  : Duty %    (0–100)
 *   Byte[2-3]: Temp      (int16_t °C × 10)
 * ============================================================ */
void CAN_Broadcast_Fan(FanCTRL_t *fan, float temp_C)
{
    if (!canstat.system_transmit_stat) return;

    int16_t temp_raw = (int16_t)(temp_C * 10.0f);  // 25.3 °C → 253

    uint8_t data[4];
    data[0] = (uint8_t)fan->Mode;
    data[1] = fan->dutycycle_pct;
    data[2] = (uint8_t)((temp_raw >> 8) & 0xFF);
    data[3] = (uint8_t)( temp_raw       & 0xFF);

    CAN_SendAll(BCAST_FAN, FDCAN_DLC_BYTES_4, data);
}

/* ============================================================
 * CAN_Broadcast_EEPROM
 *
 * [0x66B] DLC=8 — fan defaults + hs_default_state from Config
 *   Byte[0]: fan_default_mode
 *   Byte[1]: fan_default_duty
 *   Byte[2]: fan_min_duty
 *   Byte[3]: fan_auto_on_temp
 *   Byte[4]: fan_auto_off_temp
 *   Byte[5]: hs_default_state
 *   Byte[6-7]: Reserved
 * ============================================================ */
void CAN_Broadcast_EEPROM(Config *cfg)
{
    if (!canstat.system_transmit_stat) return;

    uint8_t data[8];
    data[0] = cfg->fan_default_mode;
    data[1] = cfg->fan_default_duty;
    data[2] = cfg->fan_min_duty;
    data[3] = cfg->fan_auto_on_temp;
    data[4] = cfg->fan_auto_off_temp;
    data[5] = cfg->hs_default_state;
    data[6] = 0x00;
    data[7] = 0x00;

    CAN_SendAll(BCAST_EEPROM, FDCAN_DLC_BYTES_8, data);
}

/* ============================================================
 * CAN_Broadcast_UV
 *
 * [0x66E] DLC=6 — active UV thresholds from uv_status
 *   Byte[0-1]: V24  UV threshold (uint16_t mV)
 *   Byte[2-3]: VCAP UV threshold (uint16_t mV)
 *   Byte[4-5]: V12  UV threshold (uint16_t mV)
 * ============================================================ */
void CAN_Broadcast_UV(void)
{
    if (!canstat.system_transmit_stat) return;

    uint8_t data[6];
    data[0] = (uv_status.uv_V24_mV  >> 8) & 0xFF;
    data[1] =  uv_status.uv_V24_mV        & 0xFF;
    data[2] = (uv_status.uv_VCAP_mV >> 8) & 0xFF;
    data[3] =  uv_status.uv_VCAP_mV       & 0xFF;
    data[4] = (uv_status.uv_V12_mV  >> 8) & 0xFF;
    data[5] =  uv_status.uv_V12_mV        & 0xFF;

    CAN_SendAll(BCAST_UV, FDCAN_DLC_BYTES_6, data);
}

/* ============================================================
 * CAN_Broadcast_OC_Config
 *
 * [0x66F] DLC=8 — OC thresholds RAIL_AUX … RAIL_CAP
 *   Byte[0-1]: RAIL_AUX   (uint16_t mA)
 *   Byte[2-3]: RAIL_LED   (uint16_t mA)
 *   Byte[4-5]: RAIL_DRIVE (uint16_t mA)
 *   Byte[6-7]: RAIL_CAP   (uint16_t mA)
 *
 * [0x670] DLC=2 — OC threshold RAIL_SBC
 *   Byte[0-1]: RAIL_SBC   (uint16_t mA)
 * ============================================================ */
void CAN_Broadcast_OC_Config(void)
{
    if (!canstat.system_transmit_stat) return;

    /* Frame A: RAIL_AUX, RAIL_LED, RAIL_DRIVE, RAIL_CAP */
    uint8_t dataA[8];
    dataA[0] = (oc_status.oc_threshold_mA[RAIL_AUX]   >> 8) & 0xFF;
    dataA[1] =  oc_status.oc_threshold_mA[RAIL_AUX]         & 0xFF;
    dataA[2] = (oc_status.oc_threshold_mA[RAIL_LED]   >> 8) & 0xFF;
    dataA[3] =  oc_status.oc_threshold_mA[RAIL_LED]         & 0xFF;
    dataA[4] = (oc_status.oc_threshold_mA[RAIL_DRIVE] >> 8) & 0xFF;
    dataA[5] =  oc_status.oc_threshold_mA[RAIL_DRIVE]       & 0xFF;
    dataA[6] = (oc_status.oc_threshold_mA[RAIL_CAP]   >> 8) & 0xFF;
    dataA[7] =  oc_status.oc_threshold_mA[RAIL_CAP]         & 0xFF;

    CAN_SendAll(BCAST_OC_CFG_A, FDCAN_DLC_BYTES_8, dataA);

    /* Frame B: RAIL_SBC */
    uint8_t dataB[2];
    dataB[0] = (oc_status.oc_threshold_mA[RAIL_SBC] >> 8) & 0xFF;
    dataB[1] =  oc_status.oc_threshold_mA[RAIL_SBC]       & 0xFF;

    CAN_SendAll(BCAST_OC_CFG_B, FDCAN_DLC_BYTES_2, dataB);
}

/* ============================================================
 * CAN_Broadcast_IO_Status
 *
 * [0x672] DLC=3
 *   Byte[0]: SW pin state       (0/1)
 *   Byte[1]: V_LED_PWR state    (0/1)
 *   Byte[2]: CAN relay enabled  (0/1)
 * ============================================================ */
void CAN_Broadcast_IO_Status(void)
{
    if (!canstat.system_transmit_stat) return;

    uint8_t data[3];
    data[0] = (uint8_t)HAL_GPIO_ReadPin(SW_GPIO_Port, SW_Pin);
    data[1] = (uint8_t)HAL_GPIO_ReadPin(V_LED_PWR_GPIO_Port, V_LED_PWR_Pin);
    data[2] = (uint8_t)canstat.relay_enabled;

    CAN_SendAll(BCAST_IO_STATUS, FDCAN_DLC_BYTES_3, data);
}
