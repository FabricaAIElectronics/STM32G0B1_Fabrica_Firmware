#pragma once

#include "canopen_frame.h"

static const uint8_t CanOpen_Heartbeat_HeartbeatDataLength = 1;

CanOpen_Frame CanOpen_Heartbeat_CreateHeartbeatFrame(uint8_t node_id, CanOpen_Heartbeat_Status status);
CanOpen_Frame CanOpen_Heartbeat_CreateStartStateFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Heartbeat_CreateStopStateFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Heartbeat_CreateRunStateFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Heartbeat_CreatePreOpStateFrame(uint8_t node_id);
