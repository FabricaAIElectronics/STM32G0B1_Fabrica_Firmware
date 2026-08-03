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
#include "battery.h"
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
    /* ---- System reset / bootloader ----
     * HAL note: HAL_FDCAN_GetRxMessage() puts the RAW 4-bit DLC code
     * (0..15) in the low nibble of DataLength — NOT the bit-shifted
     * FDCAN_DLC_BYTES_x form. The `dlc` parameter comes straight from
     * rxHdr.DataLength, so compare it against the integer byte count
     * (mask with 0x0F for safety against any future HAL change). */
    if (id == DEVICE_ADDR) {
        if ((data[0] == 0xFF) && ((dlc & 0x0FU) == 2U)) {
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

        /* DLC=5 form additionally carries the AUTO tuning that was previously
         * reachable only by reflashing the EEPROM defaults:
         *   byte[2] min_duty  byte[3] auto_on_temp  byte[4] auto_off_temp
         * The DLC=2 form is unchanged, so existing hosts are unaffected.
         *
         * HAL note: HAL_FDCAN_GetRxMessage() reports the RAW 4-bit DLC code in
         * the low nibble of DataLength, not the FDCAN_DLC_BYTES_x macro value,
         * so this compares against the integer byte count. Same convention as
         * the LEDDriver's VOLTAGESET handler. */
        can_rxMessage.fan_cfg_valid = 0;
        if ((dlc & 0x0FU) >= 5U) {
            uint8_t min_duty = data[2];
            uint8_t on_temp  = data[3];
            uint8_t off_temp = data[4];
            if (min_duty > 100) min_duty = 100;

            /* Hysteresis needs on > off. An inverted or equal pair would make
             * FAN_AutoControl latch on the first crossing and never release,
             * leaving the fan stuck - so the whole tuning group is rejected
             * rather than half-applied. */
            if (on_temp > off_temp) {
                can_rxMessage.fan_min_duty      = min_duty;
                can_rxMessage.fan_auto_on_temp  = on_temp;
                can_rxMessage.fan_auto_off_temp = off_temp;
                can_rxMessage.fan_cfg_valid     = 1;
            }
        }

        can_rxMessage.fan_cmd_received = 1;
    }

    /* ---- HS switch control ---- */
    if (id == CMD_HS) {
        for (uint8_t i = 0; i < RAIL_COUNT; i++) {
            can_rxMessage.hs_state[i] = (((*data>>i)&0x01) != 0) ? 1 : 0;
        }
        can_rxMessage.hs_cmd_received = 1;
    }

    /* ---- Overcurrent thresholds (mirrors BCAST_OC_CFG_A) ----
     * Four big-endian uint16 values = 8 bytes. Each field is only applied if
     * the frame is actually long enough to carry it; a short frame used to
     * read past the received data and silently zero the remaining thresholds,
     * which disables overcurrent protection on those rails. Partial updates
     * (e.g. dlc=4 to set AUX+LED only) are therefore supported and safe. */
    if (id == CMD_OC) {
        const uint8_t n = (uint8_t)(dlc & 0x0FU);
        if (n >= 2U) can_rxMessage.oc_threshold_mA[RAIL_AUX]   = ((uint16_t)data[0] << 8) | data[1];
        if (n >= 4U) can_rxMessage.oc_threshold_mA[RAIL_LED]   = ((uint16_t)data[2] << 8) | data[3];
        if (n >= 6U) can_rxMessage.oc_threshold_mA[RAIL_DRIVE] = ((uint16_t)data[4] << 8) | data[5];
        if (n >= 8U) can_rxMessage.oc_threshold_mA[RAIL_CAP]   = ((uint16_t)data[6] << 8) | data[7];
        if (n >= 2U) can_rxMessage.oc_cmd_received = 1;
    }

    /* ---- Overcurrent fault reset ---- */
    if (id == CMD_OC_RESET) {
        can_rxMessage.oc_reset_mask     = data[0];
        can_rxMessage.oc_reset_received = 1;
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

    /* ---- OLED per-page dwell ---- */
    if (id == CMD_PAGE_DWELL) {
        can_rxMessage.page_dwell[0]            = data[0];
        can_rxMessage.page_dwell[1]            = data[1];
        can_rxMessage.page_dwell[2]            = data[2];
        can_rxMessage.page_dwell_cmd_received  = 1;
    }

    /* ---- Battery SOC-low warning threshold ---- */
    if (id == CMD_BAT_CFG) {
        can_rxMessage.bat_low_soc_pct       = data[0];
        can_rxMessage.bat_cfg_cmd_received  = 1;
    }

    /* ---- Flash-over-CAN watchdog ----
     * HAL note: rxHdr.DataLength is the raw 4-bit DLC code (0..15), not
     * the bit-shifted FDCAN_DLC_BYTES_x form. Mask & compare to byte count. */
    if ((data[0] == 0xFF) && ((dlc & 0x0FU) == 1U)) {
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
 * [0x150] DLC=5
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
 * [0x151] DLC=8 — BAT, CAP, SBC, DRIVE current (uint16_t mA)
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
 * [0x155] DLC=4 — AUX, LED current (uint16_t mA)
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
 * [0x152] DLC=8
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

    /* Update UV fault flags from live measurements vs thresholds.
     * uv_fault_mask bit layout (broadcast in BCAST_VOLTAGE byte 6):
     *   bit 0 — V24  UV
     *   bit 1 — VCAP UV
     *   bit 2 — V12  UV
     *   bit 3 — SOC LOW  (filtered SOC < Battery_GetLowSocThreshold_pct,
     *                     configurable via CMD_BAT_CFG 0x147)
     *   bits 4..7 — reserved
     * The bit is set fresh every broadcast so it follows the live SOC
     * without latching. */
    uv_status.uv_fault_mask = 0;
    if (v24  < uv_status.uv_V24_mV)        uv_status.uv_fault_mask |= UV_FAULT_V24;
    if (vcap < uv_status.uv_VCAP_mV)       uv_status.uv_fault_mask |= UV_FAULT_VCAP;
    if (v12  < uv_status.uv_V12_mV)        uv_status.uv_fault_mask |= UV_FAULT_V12;
    if (Battery_IsLow(ms->battery_soc_pct)) uv_status.uv_fault_mask |= UV_FAULT_SOC_LOW;

    uint8_t data[8];
    data[0] = (v24  >> 8) & 0xFF;  data[1] = v24  & 0xFF;
    data[2] = (vcap >> 8) & 0xFF;  data[3] = vcap & 0xFF;
    data[4] = (v12  >> 8) & 0xFF;  data[5] = v12  & 0xFF;
    data[6] = uv_status.uv_fault_mask;
    data[7] = ms->battery_soc_pct;   /* 6S Li-ion SOC 0..100 % (was Reserved) */

    CAN_SendAll(BCAST_VOLTAGE, FDCAN_DLC_BYTES_8, data);
}

/* ============================================================
 * CAN_Broadcast_Fan
 *
 * [0x153] DLC=7
 *   Byte[0]  : Fan mode  (0=OFF, 1=ON, 2=AUTO)
 *   Byte[1]  : Duty %    (0–100)  -- APPLIED, not commanded
 *   Byte[2-3]: Temp      (int16_t °C × 10)
 *   Byte[4]  : min_duty      %    -- live AUTO tuning, see CMD_FAN DLC=5
 *   Byte[5]  : auto_on_temp  °C
 *   Byte[6]  : auto_off_temp °C
 *
 * Bytes 4-6 are the values the fan is running with RIGHT NOW, which is not
 * what BCAST_EEPROM (0x154) reports - that one echoes the saved Config, so a
 * threshold set over CAN and not yet committed shows up here and not there.
 * Reading tuning back from 0x154 would tell a host its command had been
 * ignored. Bytes 0-3 are unchanged, so existing consumers are unaffected.
 * ============================================================ */
void CAN_Broadcast_Fan(FanCTRL_t *f, float temp_C)
{
    if (!canstat.system_transmit_stat) return;

    int16_t temp_raw = (int16_t)(temp_C * 10.0f);  // 25.3 °C → 253

    uint8_t data[7];
    data[0] = (uint8_t)f->Mode;
    data[1] = f->dutycycle_pct;
    data[2] = (uint8_t)((temp_raw >> 8) & 0xFF);
    data[3] = (uint8_t)( temp_raw       & 0xFF);
    data[4] = f->min_dutycycle;
    data[5] = f->auto_on_temp;
    data[6] = f->auto_off_temp;

    CAN_SendAll(BCAST_FAN, FDCAN_DLC_BYTES_7, data);
}

/* ============================================================
 * CAN_Broadcast_EEPROM
 *
 * [0x154] DLC=8 — fan defaults + hs_default_state from Config
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
 * [0x156] DLC=6 — active UV thresholds from uv_status
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
 * [0x157] DLC=8 — OC thresholds RAIL_AUX … RAIL_CAP (mirrors CMD_OC)
 *   Byte[0-1]: RAIL_AUX   (uint16_t mA)
 *   Byte[2-3]: RAIL_LED   (uint16_t mA)
 *   Byte[4-5]: RAIL_DRIVE (uint16_t mA)
 *   Byte[6-7]: RAIL_CAP   (uint16_t mA)
 *
 * RAIL_SBC has no MCU-driven EN — software OC disabled, not broadcast.
 * ============================================================ */
void CAN_Broadcast_OC_Config(void)
{
    if (!canstat.system_transmit_stat) return;

    uint8_t data[8];
    data[0] = (oc_status.oc_threshold_mA[RAIL_AUX]   >> 8) & 0xFF;
    data[1] =  oc_status.oc_threshold_mA[RAIL_AUX]         & 0xFF;
    data[2] = (oc_status.oc_threshold_mA[RAIL_LED]   >> 8) & 0xFF;
    data[3] =  oc_status.oc_threshold_mA[RAIL_LED]         & 0xFF;
    data[4] = (oc_status.oc_threshold_mA[RAIL_DRIVE] >> 8) & 0xFF;
    data[5] =  oc_status.oc_threshold_mA[RAIL_DRIVE]       & 0xFF;
    data[6] = (oc_status.oc_threshold_mA[RAIL_CAP]   >> 8) & 0xFF;
    data[7] =  oc_status.oc_threshold_mA[RAIL_CAP]         & 0xFF;

    CAN_SendAll(BCAST_OC_CFG_A, FDCAN_DLC_BYTES_8, data);
}

/* ============================================================
 * CAN_Broadcast_IO_Status
 *
 * [0x159] DLC=3
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

/* ============================================================
 * CAN_Broadcast_Battery_Cfg
 *
 * [0x15A] DLC=8 — static battery configuration. Lets the host display
 * the cutoff and full reference voltages without hardcoding them, and
 * see which internal-resistance value is being used for IR compensation.
 *
 *   Byte[0-1] : V_cutoff_mV   (uint16 BE) — 0% SOC reference
 *   Byte[2-3] : V_full_mV     (uint16 BE) — 100% SOC reference
 *   Byte[4-5] : R_int_mOhm    (uint16 BE) — pack internal resistance
 *   Byte[6]   : Cell_Count    (uint8)     — series cells (6 for 6S)
 *   Byte[7]   : Reserved
 * ============================================================ */
void CAN_Broadcast_Battery_Cfg(void)
{
    if (!canstat.system_transmit_stat) return;

    const uint16_t v_cut  = BATTERY_CUTOFF_MV;
    const uint16_t v_full = BATTERY_FULL_MV;
    const uint16_t r_int  = BATTERY_INT_R_MILLIOHM;

    uint8_t data[8];
    data[0] = (v_cut  >> 8) & 0xFF;  data[1] = v_cut  & 0xFF;
    data[2] = (v_full >> 8) & 0xFF;  data[3] = v_full & 0xFF;
    data[4] = (r_int  >> 8) & 0xFF;  data[5] = r_int  & 0xFF;
    data[6] = 6U;     /* 6S Li-ion / Li-Po */
    data[7] = 0x00;

    CAN_SendAll(BCAST_BATTERY_CFG, FDCAN_DLC_BYTES_8, data);
}
