/**
  ******************************************************************************
  * @file    fdcan.c
  * @brief   FDCAN TX helper implementation.
  ******************************************************************************
  */

#include "fdcan.h"

/* ── Private helper ────────────────────────────────────────────────────── */

/**
 * @brief  Convert a byte count (0–8) to the HAL FDCAN DLC constant.
 */
static uint32_t dlc_bytes_to_fdcan_dlc(uint8_t bytes)
{
    static const uint32_t lut[9] = {
        FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
        FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
        FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8
    };
    return (bytes <= 8U) ? lut[bytes] : FDCAN_DLC_BYTES_8;
}

/* ── Public API ────────────────────────────────────────────────────────── */

bool FDCAN_SendFrame(uint32_t std_id, const uint8_t *data, uint8_t len)
{
    FDCAN_TxHeaderTypeDef tx_header;
    uint8_t tx_buf[8] = {0};
    uint8_t send_len = (len > 8U) ? 8U : len;

    memset(&tx_header, 0, sizeof(tx_header));

    tx_header.Identifier          = std_id & 0x7FFU;       /* 11-bit mask */
    tx_header.IdType              = FDCAN_STANDARD_ID;
    tx_header.TxFrameType         = FDCAN_DATA_FRAME;
    tx_header.DataLength          = dlc_bytes_to_fdcan_dlc(send_len);
    tx_header.BitRateSwitch       = FDCAN_BRS_OFF;
    tx_header.FDFormat            = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;

    if (send_len > 0U && data != NULL) {
        memcpy(tx_buf, data, send_len);
    }

    return (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_header, tx_buf) == HAL_OK);
}
