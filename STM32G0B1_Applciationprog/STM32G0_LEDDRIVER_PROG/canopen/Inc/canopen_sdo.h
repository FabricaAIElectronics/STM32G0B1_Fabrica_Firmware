#pragma once
#include "canopen_frame.h"

#define CANOPEN_SDO_INDEX_POSITION 8u
#define CANOPEN_SDO_SUB_INDEX_POSITION 24u
#define CANOPEN_SDO_DATA_BYTES_MAX_LENGTH 4u
#define CANOPEN_SDO_DATA_LENGTH 8u
#define CANOPEN_SDO_CCS_POS 5
#define CANOPEN_SDO_CCS_WRITE (uint8_t)(1 << CANOPEN_SDO_CCS_POS)
#define CANOPEN_SDO_CCS_READ (uint8_t)(2 << CANOPEN_SDO_CCS_POS)
#define CANOPEN_SDO_EXPEDITE_POS 1
#define CANOPEN_SDO_EXPEDITE (uint8_t)(1 << CANOPEN_SDO_EXPEDITE_POS)

// SDO error codes - values are taken from the spec
typedef enum {
    CANOPEN_SDO_ERR_BAD_COMMAND = 0x5040001,
    CANOPEN_SDO_ERR_NO_READ_PERM = 0x6010001,
    CANOPEN_SDO_ERR_NO_WRITE_PERM = 0x6010002,
    CANOPEN_SDO_ERR_NO_OBJ = 0x6020000,
    CANOPEN_SDO_ERR_PDO_NO_MAP = 0x6040041,
    CANOPEN_SDO_ERR_LEN_NO_MATCH = 0x6070010,
    CANOPEN_SDO_ERR_NO_SUBINDEX = 0x6090011,
    CANOPEN_SDO_ERR_INVALID_VALUE = 0x6090030,
} CanOpen_Sdo_ErrNum;

uint32_t CanOpen_Sdo_ToLowWord(uint32_t command_byte, uint32_t index, uint32_t subindex);
CanOpen_Frame CanOpen_Sdo_CreateWriteRequestFrame(
    uint8_t node_id, uint16_t index, uint8_t subindex, uint8_t data_len, uint32_t data);
CanOpen_Frame CanOpen_Sdo_CreateReadRequestFrame(uint8_t node_id, uint16_t index, uint8_t subindex);
uint32_t CanOpen_Sdo_GetSdoMessageType(const CanOpen_Frame* frame);
uint8_t GetSdoReadReceivedCommand(uint8_t data_len);
CanOpen_Frame CanOpen_Sdo_CreateReadDataResponseFrame(
    uint8_t node_id, uint16_t index, uint8_t subindex, uint8_t received_data, uint32_t data);
CanOpen_Frame CanOpen_Sdo_CreateErrorResponseFrame(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t data);
CanOpen_Frame CanOpen_Sdo_CreateWriteAckResponseFrame(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t data);
