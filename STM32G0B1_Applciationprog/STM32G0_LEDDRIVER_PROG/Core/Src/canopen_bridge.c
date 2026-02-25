/*
 * canopen_bridge.c
 *
 * Bridge between STM32 FDCAN HAL and the CANopen library.
 *
 * Object Dictionary:
 *   0x6000 sub 1-3  RW u16  PWM channels 0/1/2 (0-100%)
 *   0x6001 sub 1-2  RW u16  Undervoltage thresholds (24V, 17.5V)
 *   0x6002 sub 1-2  RO u16  Live ADC voltages (24V, 17.5V)
 *   0x6003 sub 1-2  RO u8   Device state / error code
 *   0x6004 sub 1    WO u8   EEPROM command (0x01=write, 0x02=reset default)
 *   0x6005 sub 1    WO u8   Bootloader trigger (0xFF = reset)
 */

#include "canopen_bridge.h"
#include "can_operation.h"
#include "applogic.h"

/* ---- Ring Buffers ---- */
static CanRingBuffer rx_buf;      /* SDO/PDO frames from FIFO0 ISR */
static CanRingBuffer nmt_rx_buf;  /* NMT frames from FIFO1 ISR */
static CanRingBuffer tx_buf;      /* Outgoing frames (SDO responses, heartbeats) */

/* Pointer to app state machine for live status reads */
static AppStateMachine *g_sm = NULL;

/* ---- ObjDict Callback Prototypes ---- */
static uint32_t PwmReadCb(ObjDict_Data *data, const uint8_t subindex);
static uint32_t PwmWriteCb(const uint8_t subindex, const ObjDict_Data value);
static uint32_t VoltageThreshReadCb(ObjDict_Data *data, const uint8_t subindex);
static uint32_t VoltageThreshWriteCb(const uint8_t subindex, const ObjDict_Data value);
static uint32_t AdcReadCb(ObjDict_Data *data, const uint8_t subindex);
static uint32_t DeviceStatusReadCb(ObjDict_Data *data, const uint8_t subindex);
static uint32_t EepromCmdWriteCb(const uint8_t subindex, const ObjDict_Data value);
static uint32_t BootloaderWriteCb(const uint8_t subindex, const ObjDict_Data value);

/*=======================================================================
 *  CanOpenBridge_Init
 *=======================================================================*/
void CanOpenBridge_Init(AppStateMachine *sm)
{
    g_sm = sm;

    rx_buf     = RingBuffer_Create();
    nmt_rx_buf = RingBuffer_Create();
    tx_buf     = RingBuffer_Create();

    ObjDict_Reset();

    /* 0x6000: PWM control — 3 channels, RW, 2 bytes each */
    ObjDict_CreateEntry(0x6000, 3, OBJENTRY_PERMIT_RW,
                        OBJENTRY_SIZE_SHORT, PwmReadCb, PwmWriteCb);

    /* 0x6001: Voltage thresholds — 2 channels, RW, 2 bytes each */
    ObjDict_CreateEntry(0x6001, 2, OBJENTRY_PERMIT_RW,
                        OBJENTRY_SIZE_SHORT, VoltageThreshReadCb, VoltageThreshWriteCb);

    /* 0x6002: Live ADC readings — 2 channels, RO, 2 bytes each */
    ObjDict_CreateEntry(0x6002, 2, OBJENTRY_PERMIT_R,
                        OBJENTRY_SIZE_SHORT, AdcReadCb, NULL);

    /* 0x6003: Device state + error code — 2 sub, RO, 1 byte each */
    ObjDict_CreateEntry(0x6003, 2, OBJENTRY_PERMIT_R,
                        OBJENTRY_SIZE_BYTE, DeviceStatusReadCb, NULL);

    /* 0x6004: EEPROM command — 1 sub, WO, 1 byte */
    ObjDict_CreateEntry(0x6004, 1, OBJENTRY_PERMIT_W,
                        OBJENTRY_SIZE_BYTE, NULL, EepromCmdWriteCb);

    /* 0x6005: Bootloader trigger — 1 sub, WO, 1 byte */
    ObjDict_CreateEntry(0x6005, 1, OBJENTRY_PERMIT_W,
                        OBJENTRY_SIZE_BYTE, NULL, BootloaderWriteCb);

    /* Boot-up: transition to pre-operational (triggers InitCompleteCallback) */
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
}

/*=======================================================================
 *  ISR Entry Points
 *=======================================================================*/
void CanOpenBridge_OnRxIsr(FDCAN_RxHeaderTypeDef *header, uint8_t *data)
{
    STMCanFrame stm_frame;
    stm_frame.header_.id_       = header->Identifier;
    stm_frame.header_.is_ext_id_ = 0;
    stm_frame.header_.data_len_ = 8;
    stm_frame.header_.is_rtr_   = 0;

    for (int i = 0; i < 8; i++)
        stm_frame.data_.bytes[i] = data[i];

    RingBuffer_EnqueueSTM(&rx_buf, &stm_frame);
}

void CanOpenBridge_OnNmtRxIsr(FDCAN_RxHeaderTypeDef *header, uint8_t *data)
{
    STMCanFrame stm_frame;
    stm_frame.header_.id_       = header->Identifier;
    stm_frame.header_.is_ext_id_ = 0;
    stm_frame.header_.data_len_ = 8;
    stm_frame.header_.is_rtr_   = 0;

    for (int i = 0; i < 8; i++)
        stm_frame.data_.bytes[i] = data[i];

    RingBuffer_EnqueueSTM(&nmt_rx_buf, &stm_frame);
}

/*=======================================================================
 *  CanOpenBridge_ProcessRx — call from main loop
 *=======================================================================*/
void CanOpenBridge_ProcessRx(void)
{
    CanOpen_Frame frame;

    /* Process NMT frames (from FIFO1) */
    while (RingBuffer_DequeueCanOpen(&nmt_rx_buf, &frame)) {
        CanOpen_Nmt_ProcessFrame(&frame);
    }

    /* Process SDO/PDO frames (from FIFO0) */
    while (RingBuffer_DequeueCanOpen(&rx_buf, &frame)) {
        switch (frame.message_type_) {
        case CANOPEN_SDO_REQUEST:
        {
            CanOpen_Frame response = ObjDict_HandleSdo(&frame);
            RingBuffer_EnqueueCanOpen(&tx_buf, &response);
            break;
        }
        default:
            break;
        }
    }
}

/*=======================================================================
 *  CanOpenBridge_SendPending — call from main loop
 *=======================================================================*/
void CanOpenBridge_SendPending(void)
{
    STMCanFrame stm_frame;
    while (RingBuffer_DequeueSTM(&tx_buf, &stm_frame)) {
        CAN_Send(stm_frame.header_.id_, stm_frame.data_.bytes, stm_frame.header_.data_len_);
    }
}

/*=======================================================================
 *  CanOpenBridge_SendHeartbeat — call periodically (e.g. every 500ms)
 *=======================================================================*/
void CanOpenBridge_SendHeartbeat(void)
{
    CanOpen_Frame hb;
    CanOpen_Nmt_Command state = CanOpen_Nmt_GetState();

    if (state == CANOPEN_NMT_OPERATIONAL)
        hb = CanOpen_Heartbeat_CreateRunStateFrame(CANOPEN_NODE_ID);
    else if (state == CANOPEN_NMT_PREOPERATIONAL)
        hb = CanOpen_Heartbeat_CreatePreOpStateFrame(CANOPEN_NODE_ID);
    else
        hb = CanOpen_Heartbeat_CreateStopStateFrame(CANOPEN_NODE_ID);

    RingBuffer_EnqueueCanOpen(&tx_buf, &hb);
}

/*=======================================================================
 *  NMT Weak Callback Overrides
 *=======================================================================*/
void CanOpen_Nmt_InitCompleteCallback(void)
{
    /* CANopen spec: send heartbeat on boot-up */
    CanOpen_Frame hb = CanOpen_Heartbeat_CreatePreOpStateFrame(CANOPEN_NODE_ID);
    RingBuffer_EnqueueCanOpen(&tx_buf, &hb);
}

void CanOpen_Nmt_OnResetCallback(void)
{
    ObjDict_Reset();
    if (g_sm != NULL)
        CanOpenBridge_Init(g_sm);
}

/*=======================================================================
 *  ObjDict Callbacks
 *=======================================================================*/

/* 0x6000: PWM values (sub 1=ch0, sub 2=ch1, sub 3=ch2) */
static uint32_t PwmReadCb(ObjDict_Data *data, const uint8_t subindex)
{
    if (subindex < 1 || subindex > 3)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;
    data->u16 = can_rxMessage.pwm[subindex - 1];
    return 0;
}

static uint32_t PwmWriteCb(const uint8_t subindex, const ObjDict_Data value)
{
    if (subindex < 1 || subindex > 3)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;

    uint16_t pwm = value.u16;
    if (pwm > 100) pwm = 100;

    can_rxMessage.pwm[subindex - 1] = pwm;
    can_rxMessage.newcommandreceived = 1;
    return 0;
}

/* 0x6001: Voltage thresholds (sub 1=24V, sub 2=17.5V) */
static uint32_t VoltageThreshReadCb(ObjDict_Data *data, const uint8_t subindex)
{
    if (subindex < 1 || subindex > 2)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;

    if (subindex == 1)
        data->u16 = can_rxMessage.under_voltage_24;
    else
        data->u16 = can_rxMessage.under_voltage_17_5;
    return 0;
}

static uint32_t VoltageThreshWriteCb(const uint8_t subindex, const ObjDict_Data value)
{
    if (subindex < 1 || subindex > 2)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;

    if (subindex == 1)
        can_rxMessage.under_voltage_24 = value.u16;
    else
        can_rxMessage.under_voltage_17_5 = value.u16;

    can_rxMessage.newcommandreceived = 1;
    return 0;
}

/* 0x6002: Live ADC readings (sub 1=24V, sub 2=17.5V) */
static uint32_t AdcReadCb(ObjDict_Data *data, const uint8_t subindex)
{
    if (subindex < 1 || subindex > 2)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;
    if (g_sm == NULL)
        return CANOPEN_SDO_ERR_NO_OBJ;

    if (subindex == 1)
        data->u16 = g_sm->ledStatus.voltage_24;
    else
        data->u16 = g_sm->ledStatus.voltage_17_5;
    return 0;
}

/* 0x6003: Device state and error code (sub 1=state, sub 2=error) */
static uint32_t DeviceStatusReadCb(ObjDict_Data *data, const uint8_t subindex)
{
    if (subindex < 1 || subindex > 2)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;
    if (g_sm == NULL)
        return CANOPEN_SDO_ERR_NO_OBJ;

    if (subindex == 1)
        data->u8 = (uint8_t)g_sm->state;
    else
        data->u8 = (uint8_t)g_sm->errorCode;
    return 0;
}

/* 0x6004: EEPROM command (sub 1: 0x01=write config, 0x02=reset default) */
static uint32_t EepromCmdWriteCb(const uint8_t subindex, const ObjDict_Data value)
{
    if (subindex != 1)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;

    if (value.u8 & 0x01)
        eeprom_cmd.write_eeprom_flag = 1;
    if (value.u8 & 0x02)
        eeprom_cmd.reset_default_flag = 1;

    return 0;
}

/* 0x6005: Bootloader trigger (sub 1: write 0xFF to reset into bootloader) */
static uint32_t BootloaderWriteCb(const uint8_t subindex, const ObjDict_Data value)
{
    if (subindex != 1)
        return CANOPEN_SDO_ERR_NO_SUBINDEX;

    if (value.u8 == 0xFF)
        NVIC_SystemReset();

    return 0;
}
