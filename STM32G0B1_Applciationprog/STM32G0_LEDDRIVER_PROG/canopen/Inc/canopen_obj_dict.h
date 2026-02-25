#pragma once
#include "canopen_sdo.h"

#ifndef OBJDICT_MAX_ENTRIES
#define OBJDICT_MAX_ENTRIES 16
#endif

typedef enum {
    OBJENTRY_PERMIT_R = 1,
    OBJENTRY_PERMIT_W,
    OBJENTRY_PERMIT_RW,

} ObjEntryPermissions;

typedef enum {
    OBJENTRY_SIZE_BYTE = 1,
    OBJENTRY_SIZE_SHORT = 2,
    OBJENTRY_SIZE_LONG = 4,
} ObjEntrySizeBytes;

typedef union {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
} ObjDict_Data;

/*
 * @brief struct to hold the information of an obj entry
 * if num_subindices > 0; then subindex 0 will return the number of subindides;
 */

typedef struct {
    uint16_t index_;
    uint8_t num_subindices_;
    ObjEntryPermissions permissions_;
    ObjEntrySizeBytes data_size_;
    uint32_t (*read_callback_)(ObjDict_Data*, const uint8_t);
    uint32_t (*write_callback_)(const uint8_t, const ObjDict_Data);
} ObjEntry;

/*
 * @brief Creates an obj dict entry and adds it to the obj dictionary
 * @param index: index of entry
 * @param num_subindices: number of subindices tied to this index
 * @param permissions: access permissions: read, write or read+write
 * @param data_size_: number of bytes of data this should hold
 * @param read_callback: callback when an SDO read command is received, NULL if permissions are WO
 * @param write_callback: callback when an SDO write command is received, NULL if permissions are RO
 */
size_t ObjDict_CreateEntry(uint16_t index, uint8_t num_subindices, ObjEntryPermissions permissions,
    ObjEntrySizeBytes size, uint32_t (*read_callback)(ObjDict_Data*, const uint8_t),
    uint32_t (*write_callback)(const uint8_t, const ObjDict_Data));

/*
 * @brief Empties the object dict
 */
void ObjDict_Reset();

/*
 * @brief Handles an incoming SDO frame
 * @param frame: pointer to incoming frame
 * @return the response frame after processing
 */
CanOpen_Frame ObjDict_HandleSdo(const CanOpen_Frame* frame);
