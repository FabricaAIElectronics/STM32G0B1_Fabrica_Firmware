#pragma once
#include "canopen_frame.h"

static const uint32_t CanOpen_Pdo_MaxPdoParams = 8;
static const uint32_t CanOpen_Pdo_MaxPosition = CanOpen_Pdo_MaxPdoParams * 8;

CanOpen_Frame CanOpen_Pdo_CreateOutFrame(uint8_t node_id,
    uint16_t rpdo_id,
    uint8_t data_len,
    CanOpen_FrameData data);
