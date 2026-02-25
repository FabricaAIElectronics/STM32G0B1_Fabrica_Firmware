#include "canopen_frame.h"
#include <stdint.h>

void CanOpen_Frame_CreateFrame(CanOpen_Frame* frame, uint8_t node_id, const CanOpen_MessageType message_id,
    uint8_t length, const CanOpen_FrameData* data, uint32_t timestamp) {
    frame->node_id_ = node_id;
    frame->message_type_ = message_id;
    frame->length_ = length;
    frame->data_ = *data;
    frame->timestamp_ = timestamp;
}

void CanOpen_Frame_CreateEmptyFrame(CanOpen_Frame* frame) {
    frame->node_id_ = 0;
    frame->message_type_ = CANOPEN_INVALID_ID;
    ;
    frame->length_ = 0;
    frame->data_ = (CanOpen_FrameData) { 0, 0 };
    frame->timestamp_ = 0;
}

void CanOpen_Frame_Overwrite(CanOpen_Frame* frame, uint8_t node_id, CanOpen_MessageType message_type, uint8_t length,
    const CanOpen_FrameData* data, uint32_t timestamp) {
    frame->node_id_ = node_id;
    frame->message_type_ = message_type;
    frame->length_ = length;
    frame->data_ = *data;
    frame->timestamp_ = timestamp;
}

CanOpen_Nmt_Command CanOpen_Nmt_GetCommand(const CanOpen_Frame* frame) {
    if (frame->message_type_ != CANOPEN_NMT_CONTROL) {
        return CANOPEN_NMT_INVALID_COMMAND;
    }
    CanOpen_Nmt_Command command = (CanOpen_Nmt_Command)(frame->data_.low_word_ & 0xff);
    switch (command) {
    case CANOPEN_NMT_STOP:
    case CANOPEN_NMT_OPERATIONAL:
    case CANOPEN_NMT_PREOPERATIONAL:
    case CANOPEN_NMT_RESET_NODE:
    case CANOPEN_NMT_RESET_COMMUNICATION:
        return command;
    default:
        return CANOPEN_NMT_INVALID_COMMAND;
    }
}

CanOpen_Sdo_Command CanOpen_Sdo_GetCommand(const CanOpen_Frame* frame) {
    const uint32_t command = frame->data_.low_word_ & 0xff;
    switch (command) {
    case CANOPEN_SDO_READ_PARAM:
        return CANOPEN_SDO_READ_PARAM;
    case CANOPEN_SDO_RECEIVE_PARAM_BYTE:
        return CANOPEN_SDO_RECEIVE_PARAM_BYTE;
    case CANOPEN_SDO_RECEIVE_PARAM_SHORT:
        return CANOPEN_SDO_RECEIVE_PARAM_SHORT;
    case CANOPEN_SDO_RECEIVE_PARAM_LONG:
        return CANOPEN_SDO_RECEIVE_PARAM_LONG;
    case CANOPEN_SDO_WRITE_PARAM:
        return CANOPEN_SDO_WRITE_PARAM;
    case CANOPEN_SDO_ACK:
        return CANOPEN_SDO_ACK;
    case CANOPEN_SDO_ERROR:
        return CANOPEN_SDO_ERROR;
    default:
        return CANOPEN_SDO_INVALID;
    }
}

uint16_t CanOpen_Sdo_GetIndex(const CanOpen_Frame* frame) { return (frame->data_.low_word_ >> 8) & 0xffff; }

uint8_t CanOpen_Sdo_GetSubIndex(const CanOpen_Frame* frame) { return frame->data_.low_word_ >> 24; }

uint8_t CanOpen_Sdo_GetDataLenBytes(const CanOpen_Frame* frame) {
    // len = 4 - n part of command byte
    return 4 - ((frame->data_.low_word_ >> 2) & 0x3);
}

uint32_t CanOpen_Sdo_GetData(const CanOpen_Frame* frame) {
    switch (frame->data_.low_word_ & 0xf) {
    case 0x3:
        // 4 bytes
        return frame->data_.high_word_;
    case 0xb:
        // 2 bytes
        return frame->data_.high_word_ & 0xffff;
    case 0xf:
        // 1 byte
        return frame->data_.high_word_ & 0xff;
    default:
        return 0;
    }
}

CanOpen_MessageType CanOpen_Pdo_RPDOChannelMessageType(const uint32_t channel) {
    switch (channel) {
    case 0x200:
        return CANOPEN_PDO_OUT1;
    case 0x300:
        return CANOPEN_PDO_OUT2;
    case 0x400:
        return CANOPEN_PDO_OUT3;
    case 0x500:
        return CANOPEN_PDO_OUT4;
    default:
        return CANOPEN_INVALID_ID;
    }
}

CanOpen_MessageType CanOpen_Pdo_TPDOChannelMessageType(const uint32_t channel) {
    switch (channel) {
    case 0x180:
        return CANOPEN_PDO_IN1;
    case 0x280:
        return CANOPEN_PDO_IN2;
    case 0x380:
        return CANOPEN_PDO_IN3;
    case 0x480:
        return CANOPEN_PDO_IN4;
    default:
        return CANOPEN_INVALID_ID;
    }
}

uint64_t CanOpen_Pdo_GetData(const CanOpen_Frame* frame) { return CanOpen_FrameData_AsUInt64(&frame->data_); }

CanOpen_Heartbeat_Status CanOpen_Heartbeat_GetStatus(const CanOpen_Frame* frame) {
    if (frame->message_type_ != CANOPEN_HEARTBEAT) {
        return CANOPEN_HEARTBEAT_INVALID_STATUS;
    }
    CanOpen_Heartbeat_Status status = (CanOpen_Heartbeat_Status)(frame->data_.low_word_ & 0xff);
    switch (status) {
    case CANOPEN_HEARTBEAT_START:
    case CANOPEN_HEARTBEAT_STOP:
    case CANOPEN_HEARTBEAT_RUN:
    case CANOPEN_HEARTBEAT_PREOPERATIONAL:
        return status;
    default:
        return CANOPEN_HEARTBEAT_INVALID_STATUS;
    }
}
