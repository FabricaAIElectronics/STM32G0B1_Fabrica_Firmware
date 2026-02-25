#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint16_t CanOpen_MessageTypeMask = 0x780;

typedef uint16_t CanOpen_MessageType;
#define CANOPEN_NMT_CONTROL ((CanOpen_MessageType)0x000)
#define CANOPEN_EMERGENCY ((CanOpen_MessageType)0x080)
#define CANOPEN_SDO_REQUEST ((CanOpen_MessageType)0x600)
#define CANOPEN_SDO_RESPONSE ((CanOpen_MessageType)0x580)
// Message IDs for TPDO (async updates FROM device)
#define CANOPEN_PDO_IN1 ((CanOpen_MessageType)0x180)
#define CANOPEN_PDO_IN2 ((CanOpen_MessageType)0x280)
#define CANOPEN_PDO_IN3 ((CanOpen_MessageType)0x380)
#define CANOPEN_PDO_IN4 ((CanOpen_MessageType)0x480)
// Message IDs for RPDO (multi-param writes TO device)
#define CANOPEN_PDO_OUT1 ((CanOpen_MessageType)0x200)
#define CANOPEN_PDO_OUT2 ((CanOpen_MessageType)0x300)
#define CANOPEN_PDO_OUT3 ((CanOpen_MessageType)0x400)
#define CANOPEN_PDO_OUT4 ((CanOpen_MessageType)0x500)
#define CANOPEN_HEARTBEAT ((CanOpen_MessageType)0x700)
#define CANOPEN_INVALID_ID ((CanOpen_MessageType)0xffff)

typedef struct {
    uint32_t low_word_;
    uint32_t high_word_;
} CanOpen_FrameData;

static inline uint64_t CanOpen_FrameData_AsUInt64(const CanOpen_FrameData* data_32) {
    return ((uint64_t)data_32->high_word_ << 32) | (uint64_t)data_32->low_word_;
}

typedef struct {
    uint16_t node_id_;
    CanOpen_MessageType message_type_;
    uint8_t length_;
    CanOpen_FrameData data_;
    uint32_t timestamp_;
} CanOpen_Frame;

void CanOpen_Frame_CreateEmptyFrame(CanOpen_Frame* frame);
void CanOpen_Frame_CreateFrame(CanOpen_Frame* frame, uint8_t node_id, CanOpen_MessageType message_type, uint8_t length,
    const CanOpen_FrameData* data, uint32_t timestamp);

void CanOpen_Frame_Overwrite(CanOpen_Frame* frame, uint8_t node_id, CanOpen_MessageType message_type, uint8_t length,
    const CanOpen_FrameData* data, uint32_t timestamp);

typedef uint8_t CanOpen_Nmt_Command;
#define CANOPEN_NMT_INVALID_COMMAND ((CanOpen_Nmt_Command)0X00)
#define CANOPEN_NMT_OPERATIONAL ((CanOpen_Nmt_Command)0X01)
#define CANOPEN_NMT_STOP ((CanOpen_Nmt_Command)0X02)
#define CANOPEN_NMT_PREOPERATIONAL ((CanOpen_Nmt_Command)0X80)
#define CANOPEN_NMT_RESET_NODE ((CanOpen_Nmt_Command)0X81)
#define CANOPEN_NMT_RESET_COMMUNICATION ((CanOpen_Nmt_Command)0X82)

CanOpen_Nmt_Command CanOpen_Nmt_GetCommand(const CanOpen_Frame* frame);

typedef uint8_t CanOpen_Sdo_Command;
#define CANOPEN_SDO_INVALID ((CanOpen_Sdo_Command)0x00)
#define CANOPEN_SDO_READ_PARAM ((CanOpen_Sdo_Command)0x40)
#define CANOPEN_SDO_RECEIVE_PARAM_BYTE ((CanOpen_Sdo_Command)0x4f)
#define CANOPEN_SDO_RECEIVE_PARAM_SHORT ((CanOpen_Sdo_Command)0x4b)
#define CANOPEN_SDO_RECEIVE_PARAM_LONG ((CanOpen_Sdo_Command)0x43)
#define CANOPEN_SDO_WRITE_PARAM ((CanOpen_Sdo_Command)0x23)
#define CANOPEN_SDO_WRITE_PARAM_BYTE ((CanOpen_Sdo_Command)0x2f)
#define CANOPEN_SDO_WRITE_PARAM_SHORT ((CanOpen_Sdo_Command)0x2b)
#define CANOPEN_SDO_WRITE_PARAM_LONG ((CanOpen_Sdo_Command)0x23)
#define CANOPEN_SDO_ACK ((CanOpen_Sdo_Command)0x60)
#define CANOPEN_SDO_ERROR ((CanOpen_Sdo_Command)0x80)

CanOpen_Sdo_Command CanOpen_Sdo_GetCommand(const CanOpen_Frame* frame);
uint16_t CanOpen_Sdo_GetIndex(const CanOpen_Frame* frame);
uint8_t CanOpen_Sdo_GetSubIndex(const CanOpen_Frame* frame);
uint32_t CanOpen_Sdo_GetData(const CanOpen_Frame* frame);
uint8_t CanOpen_Sdo_GetDataLenBytes(const CanOpen_Frame* frame);

CanOpen_MessageType CanOpen_Pdo_RPDOChannelMessageType(uint32_t channel);
CanOpen_MessageType CanOpen_Pdo_TPDOChannelMessageType(uint32_t channel);
uint64_t CanOpen_Pdo_GetData(const CanOpen_Frame* frame);

typedef uint8_t CanOpen_Heartbeat_Status;
#define CANOPEN_HEARTBEAT_INVALID_STATUS ((CanOpen_Heartbeat_Status)0xff)
#define CANOPEN_HEARTBEAT_START ((CanOpen_Heartbeat_Status)0x0)
#define CANOPEN_HEARTBEAT_STOP ((CanOpen_Heartbeat_Status)0x4)
#define CANOPEN_HEARTBEAT_RUN ((CanOpen_Heartbeat_Status)0x5)
#define CANOPEN_HEARTBEAT_PREOPERATIONAL ((CanOpen_Heartbeat_Status)0x7f)

CanOpen_Heartbeat_Status CanOpen_Heartbeat_GetStatus(const CanOpen_Frame* frame);
