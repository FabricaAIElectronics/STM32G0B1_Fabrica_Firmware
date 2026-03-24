/**
 * @file    CAN_Handler.c
 * @brief   CAN bus receive dispatcher, broadcast engine, and TX/RX helpers.
 *
 * @details Implements:
 *          - ISR-safe ring buffer for incoming CAN frames
 *          - Linear-search dispatch table for registered command handlers
 *          - Periodic telemetry broadcast
 *          - ACK / NACK response helpers
 *
 *          All extended CAN IDs embed the device address in bits [15:0]
 *          and the message type in bits [28:16].  See CAN_Handler.h for
 *          the full ID layout and macro definitions.
 *
 * @author  jordan
 * @date    2026-03-24
 */

#include "CAN_Handler.h"
#include "main.h"
#include "fdcan.h"
#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>

/* Module headers — each registers its CAN command handler in CAN_Handler_Init() */
#include "Fan_PWM.h"
#include "Power_Electronic.h"
#include "Endstop.h"
#include "eeprom_driver.h"
#include "error_manager.h"
#include "stm32g0xx_hal_fdcan.h"

/* ════════════════════════════════════════════════════════════════════════════
 *  Handler function type and dispatch table
 * ════════════════════════════════════════════════════════════════════════════ */

typedef void (*can_handler_t)(uint32_t id, uint8_t *params, uint8_t len);

typedef struct {
    uint32_t      id;       /* full 29-bit extended CAN ID to match */
    can_handler_t handler;  /* callback invoked when ID matches */
} can_dispatch_entry_t;

#define MAX_CAN_HANDLERS    16
static can_dispatch_entry_t dispatch_table[MAX_CAN_HANDLERS];
static uint8_t              dispatch_count = 0;

/**
 * @brief  Register a handler for a specific 29-bit CAN ID.
 * @param  ext_id  Full extended CAN ID (use CAN_EXT_ID(msg_type) macro).
 * @param  handler Callback function pointer.
 * @retval true on success, false if table is full or handler is NULL.
 */
static bool can_register_handler(uint32_t ext_id, can_handler_t handler)
{
    if (dispatch_count >= MAX_CAN_HANDLERS || handler == NULL) {
        return false;
    }

    dispatch_table[dispatch_count].id      = ext_id & 0x1FFFFFFFU;
    dispatch_table[dispatch_count].handler  = handler;
    dispatch_count++;
    return true;
}

/* Forward declaration of local test handler */
static void handle_set_test_LED(uint32_t id, uint8_t *params, uint8_t len);

/* ════════════════════════════════════════════════════════════════════════════
 *  Internal CAN frame type
 * ════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t id;        /* 29-bit extended CAN ID */
    uint8_t  dlc;       /* payload length in bytes (0–8) */
    uint8_t  data[8];   /* payload */
} can_frame_t;

/* ════════════════════════════════════════════════════════════════════════════
 *  DLC conversion (HAL FDCAN DLC field → byte count)
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Convert HAL FDCAN DataLength field to byte count.
 *
 *         Classic CAN: values 0–8 map 1:1.
 *         CAN FD: 9→12, 10→16, 11→20, 12→24, 13→32, 14→48, 15→64.
 *         We cap at 8 for Classic CAN operation.
 */
static uint8_t dlc_to_bytes(uint32_t dlc)
{
    static const uint8_t lut[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return (dlc < 16U) ? lut[dlc] : 0U;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ISR → main-loop ring buffer
 * ════════════════════════════════════════════════════════════════════════════ */

#define RBUF_SIZE       16U
#define RBUF_MASK       (RBUF_SIZE - 1U)

static volatile uint32_t rbuf_head       = 0;
static volatile uint32_t rbuf_tail       = 0;
static can_frame_t       rbuf[RBUF_SIZE];
static volatile uint32_t rbuf_drop_count = 0;  /* frames lost due to full buffer */

/**
 * @brief  Enqueue a frame (called from ISR context).
 * @retval true on success, false if buffer is full (frame dropped).
 */
static bool EnqueueCanFrame(const can_frame_t *frame)
{
    uint32_t head = rbuf_head;
    uint32_t next = (head + 1U) & RBUF_MASK;

    if (next == rbuf_tail) {
        rbuf_drop_count++;
        return false;
    }

    memcpy(&rbuf[head], frame, sizeof(can_frame_t));

    __DMB();                /* ensure data written before publishing head */
    rbuf_head = next;
    return true;
}

/**
 * @brief  Dequeue a frame (called from main loop, non-ISR).
 * @retval true if a frame was available, false if buffer is empty.
 */
static bool DequeueCanFrame(can_frame_t *out)
{
    uint32_t tail = rbuf_tail;

    if (tail == rbuf_head) {
        return false;
    }

    memcpy(out, &rbuf[tail], sizeof(can_frame_t));

    __DMB();                /* ensure copy completes before advancing tail */
    rbuf_tail = (tail + 1U) & RBUF_MASK;
    return true;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  FDCAN RX FIFO0 callback (ISR context)
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

    /* Build internal frame and enqueue for main-loop processing */
    can_frame_t frame;
    frame.id  = rx_header.Identifier;
    frame.dlc = dlc_to_bytes(rx_header.DataLength);

    if (frame.dlc > 8U) {
        frame.dlc = 8U;     /* cap to Classic CAN max */
    }

    memcpy(frame.data, rx_data, frame.dlc);
    EnqueueCanFrame(&frame);
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
     *    Filter type  : FDCAN_FILTER_MASK
     *    FilterID1    : value  — match our device ID in bits [15:0]
     *    FilterID2    : mask   — only check bits [15:0], ignore msg type
     *
     *    This accepts any message where the lower 16 bits == CAN_DEVICE_ID.
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

    /* ── Register command handlers ─── */
    can_register_handler(CAN_ID_SET_LED,                 handle_set_test_LED);
    can_register_handler(CAN_ID_SET_FAN_PWM,             CAN_Handle_set_Fan_PWM);
    can_register_handler(CAN_ID_SET_HS_DRIVE_POWER,      CAN_Handler_Set_HS_Drive_Power);
    can_register_handler(CAN_ID_SET_HS_EXTRUDER_POWER,   CAN_Handler_Set_HS_Extruder_Power);
    can_register_handler(CAN_ID_SET_HS_SCRUBBING_POWER,  CAN_Handler_Set_HS_Scrubbing_Power);
    can_register_handler(CAN_ID_SET_EEPROM_CONFIG,       CAN_Handler_EEPROM_Write_Config);
    can_register_handler(CAN_ID_READ_EEPROM_CONFIG,      CAN_Handler_EEPROM_Read_Config);
    can_register_handler(CAN_ID_DUMP_ERRORS,             CAN_Handler_Dump_Errors);
    can_register_handler(CAN_ID_RESET_ERROR,             CAN_Handler_Reset_Error);

    /* ── Enable RX interrupt for FIFO 0 ─── */
    if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        Error_Handler();
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Main-loop dispatch
 * ════════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_Dispatch_Process_One(void)
{
    can_frame_t frame;

    if (!DequeueCanFrame(&frame)) {
        return;     /* nothing to process */
    }

    if (frame.dlc == 0U) {
        return;     /* no payload — ignore */
    }

    /* Match the full 29-bit ID against registered handlers */
    uint32_t masked_id = frame.id & 0x1FFFFFFFU;

    for (uint8_t i = 0; i < dispatch_count; ++i) {
        if (dispatch_table[i].id == masked_id) {
            dispatch_table[i].handler(frame.id, frame.data, frame.dlc);
            return;
        }
    }

    /* No handler found — frame is silently dropped */
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Periodic telemetry broadcast
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

        /* Byte 6–7: reserved (0x00) */

        FDCAN_SendFrame(CAN_ID_BROADCAST_STATUS, payload, sizeof(payload));
    }

    /* ── Frame 0x601: Currents (5 bytes) ── */
    {
        uint8_t payload[5] = {0};

        /* Byte 0–1: 24V bus current (u16 LE, 0.1A units) */
        CAN_Packer_24V_Bus_Current_1DP_2Byte(&payload[0], 2);

        /* Byte 2: drive module current (u8, 0.1A) */
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_DRIVE, &payload[2], 1);

        /* Byte 3: extruder module current (u8, 0.1A) */
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_EXTRUDER, &payload[3], 1);

        /* Byte 4: scrubbing module current (u8, 0.1A) */
        CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_SCRUBBING, &payload[4], 1);

        FDCAN_SendFrame(CAN_ID_BROADCAST_CURRENTS, payload, sizeof(payload));
    }

    /* ── Frame 0x602: Temperatures (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        /* Byte 0–5: thermistors (offset-encoded: wire = degC + 40) */
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_1, &payload[0], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_2, &payload[1], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_3, &payload[2], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_4, &payload[3], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_5, &payload[4], 1);
        CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_6, &payload[5], 1);

        /* Byte 6–7: reserved (0x00) */

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

    return CAN_SUCCESS;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Test LED handler
 * ════════════════════════════════════════════════════════════════════════════ */

static void handle_set_test_LED(uint32_t id, uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return;
    }

    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    send_ack(CAN_ID_ACK_GENERAL, 0x60);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ACK / NACK helpers
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
