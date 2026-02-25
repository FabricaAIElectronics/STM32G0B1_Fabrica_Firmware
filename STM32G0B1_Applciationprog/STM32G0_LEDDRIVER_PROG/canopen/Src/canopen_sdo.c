#include "canopen_sdo.h"

uint32_t CanOpen_Sdo_ToLowWord(uint32_t command_byte, uint32_t index, uint32_t subindex) {
    return command_byte | (index << CANOPEN_SDO_INDEX_POSITION) | (subindex << CANOPEN_SDO_SUB_INDEX_POSITION);
}

CanOpen_Frame CanOpen_Sdo_CreateReadRequestFrame(uint8_t node_id, uint16_t index, uint8_t subindex) {
    /*
     * Read Status Frame:
     * -------------------------------------------------------------------------------------------------
     *| MessageID - 11 bits | other header info | D0                 | D1     | D2    | D3       | D4-7 |
     *--------------------------------------------------------------------------------------------------
     *| 0x600 + NodeID      | ...               | command            | Object index   | Subindex | data |
     *--------------------------------------------------------------------------------------------------
     *| eg Node 1 = 0x601   | ...len = 8        | 0x40               | Index          | Subindex |  0   |
     *---------------------------------------------------------------------------------------------------
     */

    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(CANOPEN_SDO_READ_PARAM, index, subindex);
    frame_data.high_word_ = 0;

    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_REQUEST, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);

    return frame;
}

CanOpen_Frame CanOpen_Sdo_CreateWriteRequestFrame(
    uint8_t node_id, uint16_t index, uint8_t subindex, uint8_t data_len, uint32_t data) {
    /*
     * Write Status Frame:
     * -------------------------------------------------------------------------------------------------
     *| MessageID - 11 bits | other header info | D0                 | D1     | D2    | D3       | D4-7 |
     *--------------------------------------------------------------------------------------------------
     *| 0x600 + NodeID      | ...               | command            | Object index   | Subindex | data |
     *--------------------------------------------------------------------------------------------------
     *| eg Node 1 = 0x601   | ...len = 8        | 0x23|n_empty_bytes | Index          | Subindex | data |
     *---------------------------------------------------------------------------------------------------
     */
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    /* SDO write command encoding:
       command_byte = 0x23 | (number of empty bytes << 2) */
    uint32_t sdo_command_data_len = (CANOPEN_SDO_DATA_BYTES_MAX_LENGTH - data_len) << 2;
    uint8_t sdo_command = (uint8_t)(CANOPEN_SDO_WRITE_PARAM | sdo_command_data_len);

    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(sdo_command, index, subindex);
    frame_data.high_word_ = data;

    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_REQUEST, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);

    return frame;
}

uint32_t CanOpen_Sdo_GetSdoMessageType(const CanOpen_Frame* frame) {
    uint32_t data_len = (CANOPEN_SDO_DATA_BYTES_MAX_LENGTH - ((frame->data_.low_word_ >> 2) & 0x3)) * 8;
    uint32_t index = (frame->data_.low_word_ >> CANOPEN_SDO_INDEX_POSITION) & 0xffff;
    uint32_t subindex = (frame->data_.low_word_ >> CANOPEN_SDO_SUB_INDEX_POSITION) & 0xff;

    return (index << 16) | (subindex << 8) | data_len;
}

uint8_t GetSdoReadReceivedCommand(uint8_t data_len) {
    switch (data_len) {
    case 1:
        return CANOPEN_SDO_RECEIVE_PARAM_BYTE;
    case 2:
        return CANOPEN_SDO_RECEIVE_PARAM_SHORT;
    case 4:
        return CANOPEN_SDO_RECEIVE_PARAM_LONG;
    default:
        return CANOPEN_SDO_ERROR;
    }
}

CanOpen_Frame CanOpen_Sdo_CreateReadDataResponseFrame(
    uint8_t node_id, uint16_t index, uint8_t subindex, uint8_t received_data_len, uint32_t data) {
    /*
     * Read Response Status Frame:
     * -------------------------------------------------------------------------------------------------
     *| MessageID - 11 bits | other header info | D0                 | D1     | D2    | D3       | D4-7 |
     *--------------------------------------------------------------------------------------------------
     *| 0x580 + NodeID      | ...               | command            | Object index   | Subindex | data |
     *--------------------------------------------------------------------------------------------------
     *| eg Node 1 = 0x581   | ...len = 8        | 0x43               | Index          | Subindex | data |
     *---------------------------------------------------------------------------------------------------
     */
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    uint8_t received_command = GetSdoReadReceivedCommand(received_data_len);

    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(received_command, index, subindex);
    frame_data.high_word_ = data;

    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_RESPONSE, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);

    return frame;
}

CanOpen_Frame CanOpen_Sdo_CreateErrorResponseFrame(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t data) {
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data = { 0 };
    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(0x80, index, subindex);
    frame_data.high_word_ = data;
    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_RESPONSE, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);
    return frame;
}

CanOpen_Frame CanOpen_Sdo_CreateWriteAckResponseFrame(
    uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t data) {
    /*
     * Write Response Status Frame:
     * -------------------------------------------------------------------------------------------------
     *| MessageID - 11 bits | other header info | D0                 | D1     | D2    | D3       | D4-7 |
     *--------------------------------------------------------------------------------------------------
     *| 0x580 + NodeID      | ...               | command            | Object index   | Subindex | data |
     *--------------------------------------------------------------------------------------------------
     *| eg Node 1 = 0x581   | ...len = 8        | 0x60               | Index          | Subindex | 0    |
     *---------------------------------------------------------------------------------------------------
     */
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(CANOPEN_SDO_ACK, index, subindex);
    frame_data.high_word_ = data;
    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_RESPONSE, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);

    return frame;
}

CanOpen_Frame CanOpen_Sdo_CreateWriteErrorResponseFrame(uint8_t node_id, uint16_t index, uint8_t subindex) {
    /*
     * Write Response Status Frame:
     * -------------------------------------------------------------------------------------------------
     *| MessageID - 11 bits | other header info | D0                 | D1     | D2    | D3       | D4-7 |
     *--------------------------------------------------------------------------------------------------
     *| 0x580 + NodeID      | ...               | command            | Object index   | Subindex | data |
     *--------------------------------------------------------------------------------------------------
     *| eg Node 1 = 0x581   | ...len = 8        | 0x80               | Index          | Subindex | 0    |
     *---------------------------------------------------------------------------------------------------
     */
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    frame_data.low_word_ = CanOpen_Sdo_ToLowWord(CANOPEN_SDO_ERROR, index, subindex);
    frame_data.high_word_ = 0;
    CanOpen_Frame_CreateFrame(&frame, node_id, CANOPEN_SDO_RESPONSE, CANOPEN_SDO_DATA_LENGTH, &frame_data, 0);

    return frame;
}
