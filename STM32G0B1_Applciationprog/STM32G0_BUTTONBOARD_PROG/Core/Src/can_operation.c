/**
  ******************************************************************************
  * @file    can_operation.c
  * @brief   FDCAN transport, RX dispatch and periodic broadcasts.
  *
  * Multi-byte fields are little-endian, matching the existing Knob.dbc from
  * the STM32F303 build (`@1+` Intel byte order) rather than the big-endian
  * convention PowerStage uses for its threshold frames.
  ******************************************************************************
  */

#include "can_operation.h"
#include "inputs.h"
#include "leds.h"
#include "main.h"
#include <string.h>

static FDCAN_TxHeaderTypeDef txMsgHeader = {
    .IdType              = FDCAN_STANDARD_ID,
    .TxFrameType         = FDCAN_DATA_FRAME,
    .DataLength          = FDCAN_DLC_BYTES_8,
    .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
    .BitRateSwitch       = FDCAN_BRS_OFF,
    .FDFormat            = FDCAN_CLASSIC_CAN,
    .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
    .MessageMarker       = 0
};

static FDCAN_RxHeaderTypeDef rxMsgHeader;
static uint8_t txMsgData[8];
static uint8_t rxMsgData[8];

CAN_RXMessage can_rxMessage = {0};

static volatile uint32_t lastFlashMsgTick = 0U;
static uint8_t heartbeat = 0U;

/* HAL wants DataLength as one of the bit-positioned FDCAN_DLC_BYTES_x
 * constants (FDCAN_DLC_BYTES_8 == 0x00080000), NOT a raw byte count. Passing
 * the count straight through makes the peripheral emit DLC=0 frames, which
 * decode as empty in every analyser and are maddening to chase. */
static const uint32_t dlc_lut[9] = {
    FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
    FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
    FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8
};

void CAN_Init(void)
{
    /* Accept the whole 0x780-0x7BF sub-block in one range filter, so adding a
     * command id later needs no filter change. MX_FDCAN1_Init() must declare
     * StdFiltersNbr >= 1 for this to take effect. */
    FDCAN_FilterTypeDef filt = {0};
    filt.IdType       = FDCAN_STANDARD_ID;
    filt.FilterIndex  = 0U;
    filt.FilterType   = FDCAN_FILTER_RANGE;
    filt.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filt.FilterID1    = BTN_CAN_ID_RANGE_LOW;
    filt.FilterID2    = BTN_CAN_ID_RANGE_HIGH;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filt);

    /* Everything outside the filter is rejected, extended frames included.
     * This board shares a bus with CANopen traffic, so a permissive global
     * filter would flood the RX FIFO with frames it must ignore anyway. */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT, FDCAN_REJECT,
                                 FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0U);
    HAL_FDCAN_Start(&hfdcan1);
}

void CAN_Send(uint32_t id, uint8_t *data, uint8_t len)
{
    if (len > 8U) {
        len = 8U;
    }

    /* Non-blocking. If the TX FIFO is full the frame is dropped rather than
     * spinning: every sender here is a periodic broadcast, so the next tick
     * carries fresher data than the one being dropped. */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U) {
        return;
    }

    txMsgHeader.Identifier = id;
    txMsgHeader.DataLength = dlc_lut[len];
    memcpy(txMsgData, data, len);
    (void)HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txMsgHeader, txMsgData);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U) {
        return;
    }

    /* Clear before every receive. HAL_FDCAN_GetRxMessage() writes only DLC
     * bytes into a file-scope buffer, so without this a short frame leaves
     * the previous frame's bytes visible underneath it. */
    memset(rxMsgData, 0, sizeof(rxMsgData));

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxMsgHeader,
                               rxMsgData) != HAL_OK) {
        return;
    }

    /* HAL note: on receive, DataLength holds the RAW 4-bit DLC code in its
     * low nibble — not the bit-shifted FDCAN_DLC_BYTES_x form used on
     * transmit. Mask before comparing against a byte count. */
    const uint8_t dlc = (uint8_t)(rxMsgHeader.DataLength & 0x0FU);

    switch (rxMsgHeader.Identifier) {

    case BTN_DEVICEID:
        /* XCP CONNECT: byte0 = 0xFF with DLC = 2 means "go to bootloader".
         * Reset here; OpenBLT re-handles the identical frame on the identical
         * id once it is running, so the host sends it exactly once whether
         * the application or the bootloader is currently in charge. */
        if ((rxMsgData[0] == 0xFFU) && (dlc == 2U)) {
            NVIC_SystemReset();
        }
        /* byte0 = 0xFF with DLC = 1 is ordinary XCP polling during a
         * download. Note it so the application stops broadcasting. */
        if ((rxMsgData[0] == 0xFFU) && (dlc == 1U)) {
            can_rxMessage.flashdetected = 1U;
            lastFlashMsgTick = HAL_GetTick();
        }
        break;

    case BTN_CMD_LED:
        /* Reject a short frame rather than acting on a zeroed byte 1: a
         * DLC=1 frame would otherwise read as "turn all internal LEDs off",
         * which is a command the host never sent. */
        if (dlc >= 2U) {
            can_rxMessage.led_mask     = (uint8_t)(rxMsgData[0] & 0x3FU);
            can_rxMessage.led_int_mask = (uint8_t)(rxMsgData[1] & 0x0FU);
            can_rxMessage.led_update   = 1U;
        }
        break;

    case BTN_CMD_BUFFER:
        if (dlc >= 1U) {
            can_rxMessage.buffer_source =
                (rxMsgData[0] != 0U) ? (uint8_t)LED_SRC_STM32
                                     : (uint8_t)LED_SRC_DIRECT;
            can_rxMessage.buffer_update = 1U;
        }
        break;

    case BTN_CMD_ENCODER:
        /* byte0 bit0 = "apply the count in bytes 1..2". Anything else is
         * reserved; ignoring unknown flag bits keeps old hosts working. */
        if ((dlc >= 3U) && ((rxMsgData[0] & 0x01U) != 0U)) {
            can_rxMessage.encoder_preset =
                (int16_t)((uint16_t)rxMsgData[1] |
                          ((uint16_t)rxMsgData[2] << 8));
            can_rxMessage.encoder_preset_update = 1U;
        }
        break;

    case BTN_CMD_EEPROM:
        if (dlc >= 1U) {
            if ((rxMsgData[0] & 0x01U) != 0U) {
                can_rxMessage.eeprom_save = 1U;
            }
            if ((rxMsgData[0] & 0x02U) != 0U) {
                can_rxMessage.eeprom_load_defaults = 1U;
            }
        }
        break;

    default:
        /* In range but not a command we implement. Ignore. */
        break;
    }
}

void CAN_FlashDetectionTick(void)
{
    if ((HAL_GetTick() - lastFlashMsgTick) > 500U) {
        can_rxMessage.flashdetected = 0U;
    }
}

/* --------------------------------------------------------------------------
 * Broadcasts
 * -------------------------------------------------------------------------- */

void CAN_BroadcastKnobState(const InputState *in, uint8_t state,
                            uint8_t buff_sel_nibble)
{
    uint8_t d[8];
    const uint16_t pos = (uint16_t)in->encoder_pos;

    d[0] = (uint8_t)(pos & 0xFFU);
    d[1] = (uint8_t)((pos >> 8) & 0xFFU);
    d[2] = in->encoder_button;
    d[3] = in->rotary_pos;                 /* 0..6 or ROTARY_POS_INVALID     */
    d[4] = in->rotary_raw;                 /* one-hot, for diagnosing RS1    */
    d[5] = in->toggle_mask;
    d[6] = (uint8_t)(((state & 0x0FU) << 4) | (buff_sel_nibble & 0x0FU));
    d[7] = heartbeat++;                    /* free-running; wraps naturally  */

    CAN_Send(BTN_BCAST_KNOBSTATE, d, 8U);
}

void CAN_BroadcastButtons(const InputState *in)
{
    uint8_t d[2];
    d[0] = in->button_mask;
    d[1] = in->button_int_mask;
    CAN_Send(BTN_BCAST_BUTTONS, d, 2U);
}

void CAN_BroadcastLedState(uint8_t led_mask, uint8_t led_int_mask,
                           uint8_t source)
{
    uint8_t d[3];
    d[0] = led_mask;
    d[1] = led_int_mask;
    d[2] = source;
    CAN_Send(BTN_BCAST_LEDSTATE, d, 3U);
}

void CAN_BroadcastDeviceStatus(uint8_t state, uint8_t errorMask)
{
    uint8_t d[2];
    d[0] = state;
    d[1] = errorMask;
    CAN_Send(BTN_BCAST_DEVSTATUS, d, 2U);
}

void CAN_BroadcastEepromData(const Config *config)
{
    uint8_t d[8] = {0};
    d[0] = (uint8_t)(config->magic & 0xFFU);
    d[1] = (uint8_t)((config->magic >> 8) & 0xFFU);
    d[2] = config->default_led_mask;
    d[3] = config->default_led_int_mask;
    d[4] = config->default_led_source;
    d[5] = config->flags;
    d[6] = 0U;
    d[7] = 0U;
    CAN_Send(BTN_BCAST_EEPROMDATA, d, 8U);
}
