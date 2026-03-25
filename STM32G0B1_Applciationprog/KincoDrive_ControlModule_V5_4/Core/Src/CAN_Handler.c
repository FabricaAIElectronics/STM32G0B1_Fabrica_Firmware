/**
 * @file    CAN_Handler.c
 * @brief   CAN bus broadcast-only test mode.
 *
 * @details TEST MODE — no master/host commands accepted.
 *          On power-up, the device automatically broadcasts all system
 *          telemetry on a fixed period:
 *
 *          0x600  System status  (endstops, voltages, protection state)
 *          0x601  Currents       (24V bus + per-module)
 *          0x602  Temperatures   (6x PTC thermistors)
 *          0x603  Fan speeds     (5x tachometer %)
 *          0x604  GPIO status    (HS enable/PG/fault, VBUCK, ESTOP, endstop raw)
 *          0x605  Raw ADC        (12 channels, 4-bit scaled)
 *
 *          Bootloader entry is still supported (device ID + 0xFF).
 *
 * @author  jordan
 * @date    2026-03-25
 */

#include "CAN_Handler.h"
#include "main.h"
#include "fdcan.h"
#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>

/* Module headers for telemetry packing */
#include "Fan_PWM.h"
#include "Power_Electronic.h"
#include "Endstop.h"
#include "ESTOP.h"
#include "error_manager.h"
#include "stm32g0xx_hal_fdcan.h"

/* ════════════════════════════════════════════════════════════════════════════
 *  FDCAN RX FIFO0 callback (ISR context) — bootloader entry only
 * ════════════════════════════════════════════════════════════════════════════ */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0) {
        return;
    }

    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
        return;
    }

    /* Bootloader entry: device ID with payload[0] == 0xFF triggers system reset */
    if ((rx_header.Identifier == CAN_ID_BOOTLOADER) && (rx_data[0] == 0xFF)) {
        HAL_NVIC_SystemReset();
    }

    /* All other frames are ignored in test mode */
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Vector table remap (for bootloader compatibility)
 * ════════════════════════════════════════════════════════════════════════════ */

static void VectorBase_Config(void)
{
    extern const unsigned long g_pfnVectors[];
    SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Initialization
 * ════════════════════════════════════════════════════════════════════════════ */

void Pre_CAN_Handler_Init(void)
{
    VectorBase_Config();
}

void CAN_Handler_Init(void)
{
    /* ── Configure RX filter for extended IDs addressed to this device ───
     *
     *    Accepts any extended frame where bits [15:0] == CAN_DEVICE_ID.
     *    In test mode this is only used for bootloader entry detection.
     */
    FDCAN_FilterTypeDef filter_cfg = {0};
    filter_cfg.IdType       = FDCAN_EXTENDED_ID;
    filter_cfg.FilterIndex  = 0;
    filter_cfg.FilterType   = FDCAN_FILTER_MASK;
    filter_cfg.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter_cfg.FilterID1    = CAN_DEVICE_ID;       /* value: device ID in lower bits */
    filter_cfg.FilterID2    = 0x0000FFFFU;          /* mask: check lower 16 bits only */

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter_cfg) != HAL_OK) {
        Error_Handler();
    }

    /* Reject frames that don't match any filter */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT,          /* non-matching std */
                                 FDCAN_REJECT,          /* non-matching ext */
                                 FDCAN_REJECT_REMOTE,   /* remote std */
                                 FDCAN_REJECT_REMOTE);  /* remote ext */

    /* No command handlers registered — test mode is broadcast-only */

    /* ── Enable RX interrupt for FIFO 0 (bootloader entry) ─── */
    if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        Error_Handler();
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Main-loop dispatch — no-op in test mode
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_Dispatch_Process_One(void)
{
    /* No command handlers in test mode — nothing to dispatch */
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Periodic telemetry broadcast (test mode)
 * ════════════════════════════════════════════════════════════════════════════ */

int CAN_Handler_Broadcast(uint32_t period_ms)
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if ((now - last_tick) < period_ms) {
        return CAN_SUCCESS;
    }
    last_tick = now;

    /* ── Frame 0x600: System Status (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        /* Byte 0–1: endstop triggered + fault */
        CAN_Packer_Endstop_and_ESTOP_2Byte(&payload[0], &payload[1], 2);

        /* Byte 2–3: 24V bus voltage (u16 LE, 0.1V units) */
        CAN_Packer_24V_Bus_2Byte(&payload[2], 2);

        /* Byte 4: 12V bus voltage (u8, 0.1V units) */
        CAN_Packer_12V_Bus_1Byte(&payload[4], 1);

        /* Byte 5: protection state (OC + OV packed) */
        CAN_Packer_Protection_State_1Byte(&payload[5], 1);

        /* Byte 6: system state (0=NORMAL,1=WARNING,2=ERROR,3=RECOVERY) */
        payload[6] = (uint8_t)Error_Manager_GetState();

        /* Byte 7: error count */
        payload[7] = Error_Manager_GetCount();

        FDCAN_SendFrame(CAN_ID_BROADCAST_STATUS, payload, sizeof(payload));
    }

    /* ── Frame 0x601: Currents (5 bytes) ── */
    {
        uint8_t payload[5] = {0};

        CAN_Packer_24V_Bus_Current_1DP_2Byte(&payload[0], 2);
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_DRIVE, &payload[2], 1);
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_EXTRUDER, &payload[3], 1);
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_SCRUBBING, &payload[4], 1);

        FDCAN_SendFrame(CAN_ID_BROADCAST_CURRENTS, payload, sizeof(payload));
    }

    /* ── Frame 0x602: Temperatures (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_1, &payload[0], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_2, &payload[1], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_3, &payload[2], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_4, &payload[3], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_5, &payload[4], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_6, &payload[5], 1);

        FDCAN_SendFrame(CAN_ID_BROADCAST_TEMPS, payload, sizeof(payload));
    }

    /* ── Frame 0x603: Fan Speeds (5 bytes) ── */
    {
        uint8_t payload[5] = {0};

        CAN_Packer_Fan_Speed_1Byte(FAN_DR, &payload[0], 1);
        CAN_Packer_Fan_Speed_1Byte(FAN_EP, &payload[1], 1);
        CAN_Packer_Fan_Speed_1Byte(FAN_EH, &payload[2], 1);
        CAN_Packer_Fan_Speed_1Byte(FAN_ST, &payload[3], 1);
        CAN_Packer_Fan_Speed_1Byte(FAN_SF, &payload[4], 1);

        FDCAN_SendFrame(CAN_ID_BROADCAST_FANS, payload, sizeof(payload));
    }

    /* ── Frame 0x604: GPIO Status (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        /*
         * Byte 0: High-side Enable pins (active = module is commanded ON)
         *   Bit 0 = HS_DR_EN (Drive)
         *   Bit 1 = HS_E_EN  (Extruder)
         *   Bit 2 = HS_SC_EN (Scrubbing)
         *   Bit 3 = VBUCK_CTRL (12V buck)
         */
        if (HAL_GPIO_ReadPin(HS_DR_EN_GPIO_Port, HS_DR_EN_Pin))  payload[0] |= 0x01;
        if (HAL_GPIO_ReadPin(HS_E_EN_GPIO_Port,  HS_E_EN_Pin))   payload[0] |= 0x02;
        if (HAL_GPIO_ReadPin(HS_SC_EN_GPIO_Port, HS_SC_EN_Pin))  payload[0] |= 0x04;
        if (HAL_GPIO_ReadPin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin)) payload[0] |= 0x08;

        /*
         * Byte 1: High-side Power-Good pins (active LOW = power good)
         *   Bit 0 = HS_DR_PG
         *   Bit 1 = HS_E_PG
         *   Bit 2 = HS_SC_PG
         */
        if (HAL_GPIO_ReadPin(HS_DR_PG_GPIO_Port, HS_DR_PG_Pin) == GPIO_PIN_RESET)  payload[1] |= 0x01;
        if (HAL_GPIO_ReadPin(HS_E_PG_GPIO_Port,  HS_E_PG_Pin)  == GPIO_PIN_RESET)  payload[1] |= 0x02;
        if (HAL_GPIO_ReadPin(HS_SC_PG_GPIO_Port, HS_SC_PG_Pin) == GPIO_PIN_RESET)  payload[1] |= 0x04;

        /*
         * Byte 2: High-side Fault pins (active LOW = fault present)
         *   Bit 0 = HS_DR_FT
         *   Bit 1 = HS_E_FT
         *   Bit 2 = HS_SC_FT
         */
        if (HAL_GPIO_ReadPin(HS_DR_FT_GPIO_Port, HS_DR_FT_Pin) == GPIO_PIN_RESET)  payload[2] |= 0x01;
        if (HAL_GPIO_ReadPin(HS_E_FT_GPIO_Port,  HS_E_FT_Pin)  == GPIO_PIN_RESET)  payload[2] |= 0x02;
        if (HAL_GPIO_ReadPin(HS_SC_FT_GPIO_Port, HS_SC_FT_Pin) == GPIO_PIN_RESET)  payload[2] |= 0x04;

        /*
         * Byte 3: ESTOP and endstop raw GPIO states
         *   Bit 0 = ESTOP_NO  (normally open)
         *   Bit 1 = ESTOP_NC  (normally closed)
         *   Bit 2 = ESTOP debounced state (0=OK, 1=triggered)
         */
        if (HAL_GPIO_ReadPin(EStop_NO_INT_GPIO_Port, EStop_NO_INT_Pin))  payload[3] |= 0x01;
        if (HAL_GPIO_ReadPin(EStop_NC_INT_GPIO_Port, EStop_NC_INT_Pin))  payload[3] |= 0x02;
        if (ESTOP_State())  payload[3] |= 0x04;

        /*
         * Byte 4: Endstop raw states (each bit = GPIO pin state)
         *   Bit 0 = EH_H_NO  (Extruder Height Top NO)
         *   Bit 1 = EH_H_NC  (Extruder Height Top NC)
         *   Bit 2 = EH_L_NO  (Extruder Height Bottom NO)
         *   Bit 3 = EP_H_NO  (Extruder Platform Top NO)
         *   Bit 4 = EP_H_NC  (Extruder Platform Top NC)
         *   Bit 5 = EP_L_NO  (Extruder Platform Bottom NO)
         *   Bit 6 = EP_L_NC  (Extruder Platform Bottom NC)
         *   Bit 7 = SC_H_NO  (Scrubbing Front Top NO)
         */
        if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NO_INT_GPIO_Port,  ENDSTOP_EH_H_NO_INT_Pin))   payload[4] |= 0x01;
        if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NC_INT_GPIO_Port,  ENDSTOP_EH_H_NC_INT_Pin))   payload[4] |= 0x02;
        if (HAL_GPIO_ReadPin(ENDSTOP_EH_L_NO_INT_GPIO_Port,  ENDSTOP_EH_L_NO_INT_Pin))   payload[4] |= 0x04;
        if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NO_INT_GPIO_Port,  ENDSTOP_EP_H_NO_INT_Pin))   payload[4] |= 0x08;
        if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NC_INT_GPIO_Port,  ENDSTOP_EP_H_NC_INT_Pin))   payload[4] |= 0x10;
        if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NO_INT_GPIO_Port,  ENDSTOP_EP_L_NO_INT_Pin))   payload[4] |= 0x20;
        if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NC_INT_GPIO_Port,  ENDSTOP_EP_L_NC_INT_Pin))   payload[4] |= 0x40;
        if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NO_INT_GPIO_Port,  ENDSTOP_SC_H_NO_INT_Pin))   payload[4] |= 0x80;

        /*
         * Byte 5: Endstop raw states continued
         *   Bit 0 = SC_H_NC  (Scrubbing Front Top NC)
         */
        if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NC_INT_GPIO_Port,  ENDSTOP_SC_H_NC_INT_Pin))   payload[5] |= 0x01;

        /* Byte 6: LED state */
        if (HAL_GPIO_ReadPin(LED_OUT_GPIO_Port, LED_OUT_Pin))  payload[6] = 0x01;

        /* Byte 7: Blue button */
        if (HAL_GPIO_ReadPin(BlueButton_GPIO_Port, BlueButton_Pin))  payload[7] = 0x01;

        FDCAN_SendFrame(CAN_ID_EEPROM_ACK, payload, sizeof(payload));
    }

    /* ── Frame 0x605: Raw ADC values (6 bytes, 12 channels × 4 bits) ── */
    {
        extern volatile uint16_t ADC_VAL[];
        uint8_t payload[6] = {0};

        /*
         * Pack 12 ADC channels into 6 bytes (4 bits each, scaled from 12-bit → 4-bit).
         * Each nibble = adc_raw >> 8 (0–15).
         *
         * Byte 0: [PTC1_hi | PTC2_lo]   Byte 1: [PTC3_hi | PTC4_lo]
         * Byte 2: [PTC5_hi | PTC6_lo]   Byte 3: [CURR1_hi | CURR2_lo]
         * Byte 4: [CURR3_hi | V24_lo]   Byte 5: [V12_hi | CURR24V_lo]
         */
        for (int i = 0; i < 12; i++) {
            uint8_t nibble = (uint8_t)(ADC_VAL[i] >> 8);  /* 12-bit → 4-bit */
            if (i % 2 == 0) {
                payload[i / 2] |= (nibble << 4);   /* high nibble */
            } else {
                payload[i / 2] |= (nibble & 0x0F); /* low nibble */
            }
        }

        FDCAN_SendFrame(CAN_ID_EEPROM_ACK2, payload, sizeof(payload));
    }

    return CAN_SUCCESS;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ACK / NACK helpers — kept for API compatibility but unused in test mode
 * ════════════════════════════════════════════════════════════════════════════ */

void send_ack(uint32_t dst_id, uint8_t cmd)
{
    uint8_t tx[2] = { cmd, 0x00U };
    FDCAN_SendFrame(dst_id, tx, 2);
}

void send_nack(uint32_t dst_id, uint8_t cmd)
{
    uint8_t tx[2] = { cmd, 0xFFU };
    FDCAN_SendFrame(dst_id, tx, 2);
}
