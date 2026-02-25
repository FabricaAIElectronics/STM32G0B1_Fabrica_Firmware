#pragma once
#include <stdint.h>
typedef struct {
    uint32_t low;
    uint32_t high;
} __Can_Data_Ints;

typedef union {
    uint8_t bytes[8];
    __Can_Data_Ints words;
} Can_Data;

typedef struct {
    uint32_t id_;
    uint32_t is_ext_id_;
    uint32_t data_len_;
    uint32_t is_rtr_;
} CanHeader;

typedef struct {
    CanHeader header_;
    Can_Data data_;
} STMCanFrame;
