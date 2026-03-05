#include "can_operation.h"
#include "canopen_bridge.h"
#include "main.h"
#include "stm32g0b1xx.h"
#include <string.h>

static FDCAN_TxHeaderTypeDef txMsgHeader = {
    .IdType = FDCAN_STANDARD_ID,
    .DataLength = FDCAN_DLC_BYTES_8,
    .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
    .BitRateSwitch = FDCAN_BRS_OFF,
    .FDFormat = FDCAN_CLASSIC_CAN,
    .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
    .MessageMarker = 0
};
static FDCAN_RxHeaderTypeDef rxMsgHeader;
static uint8_t txMsgData[8];
static uint8_t rxMsgData[8];

CAN_RXMessage can_rxMessage = {0};
eeprom_command eeprom_cmd = {0};
static volatile uint32_t lastFlashMsgTick = 0;

void CAN_Init(void)
{
	HAL_FDCAN_Stop(&canHandle);
    /* Filter 0: NMT control (COB-ID 0x000 exact match) → FIFO1 */
    FDCAN_FilterTypeDef nmtFilter;
    nmtFilter.IdType       = FDCAN_STANDARD_ID;
    nmtFilter.FilterIndex  = 0;
    nmtFilter.FilterType   = FDCAN_FILTER_MASK;
    nmtFilter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
    nmtFilter.FilterID1    = 0x000;
    nmtFilter.FilterID2    = 0x7FF;
    HAL_FDCAN_ConfigFilter(&canHandle, &nmtFilter);

    /* Filter 1: All other standard-ID frames → FIFO0 */
    FDCAN_FilterTypeDef stdFilter;
    stdFilter.IdType       = FDCAN_STANDARD_ID;
    stdFilter.FilterIndex  = 1;
    stdFilter.FilterType   = FDCAN_FILTER_MASK;
    stdFilter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    stdFilter.FilterID1    = 0x000;
    stdFilter.FilterID2    = 0x000;  /* mask=0 → accept all standard IDs */
    HAL_FDCAN_ConfigFilter(&canHandle, &stdFilter);

    /* Enable RX notifications on both FIFOs */
    HAL_FDCAN_ActivateNotification(&canHandle,FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);
//    HAL_FDCAN_ActivateNotification(&canHandle,FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 1);
    HAL_FDCAN_Start(&canHandle);
}

void CAN_Send(uint32_t id, uint8_t* data, uint8_t len)
{
    txMsgHeader.Identifier = id;
    txMsgHeader.DataLength = len;
    memcpy(txMsgData, data, len);
    uint32_t timeout = HAL_GetTick() + 1000;
    while (HAL_FDCAN_GetTxFifoFreeLevel(&canHandle) == 0) {
        if (HAL_GetTick() > timeout) {
            return;
        }
    }
    HAL_FDCAN_AddMessageToTxFifoQ(&canHandle, &txMsgHeader, txMsgData);
}

/* FIFO0 callback: SDO, PDO, heartbeat frames */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    memset(rxMsgData, 0, sizeof(rxMsgData));
    if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) {
        HAL_FDCAN_GetRxMessage(&canHandle, FDCAN_RX_FIFO0, &rxMsgHeader, rxMsgData);

        /* OpenBLT bootloader trigger: raw frame with data[0]=0xFF, DLC=2 */
        if (rxMsgData[0] == 0xFF && rxMsgHeader.DataLength == FDCAN_DLC_BYTES_2) {
            NVIC_SystemReset();
        }

        CanOpenBridge_OnRxIsr(&rxMsgHeader, rxMsgData);
    }
}

/* FIFO1 callback: NMT control frames only */
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo1ITs)
{
    uint8_t data[8] = {0};
    FDCAN_RxHeaderTypeDef header;
    if (RxFifo1ITs & FDCAN_IT_RX_FIFO1_NEW_MESSAGE) {
        HAL_FDCAN_GetRxMessage(&canHandle, FDCAN_RX_FIFO1, &header, data);
        CanOpenBridge_OnNmtRxIsr(&header, data);
    }
}

void FOCdetection(void)
{
    if (HAL_GetTick() - lastFlashMsgTick > 500) {
        can_rxMessage.flashdetected = 0;
    }
}
