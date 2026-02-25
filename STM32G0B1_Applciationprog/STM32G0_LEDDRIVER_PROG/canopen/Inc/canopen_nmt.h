#pragma once
#include "canopen_frame.h"

static const uint16_t CanOpen_Nmt_ControlCobId = 0x0;
static const uint32_t CanOpen_Nmt_ControlDataLength = 0x2;
static const uint32_t CanOpen_Nmt_NodeIdPosition = 8;

void CanOpen_Nmt_SetState(CanOpen_Nmt_Command state);
CanOpen_Nmt_Command CanOpen_Nmt_GetState();
void CanOpen_Nmt_ProcessFrame(CanOpen_Frame* frame);

CanOpen_Frame CanOpen_Nmt_CreateStartNodeFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Nmt_CreateStopFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Nmt_CreatePreOpFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Nmt_CreateResetNodeFrame(uint8_t node_id);
CanOpen_Frame CanOpen_Nmt_CreateResetCommunicationsFrame(uint8_t node_id);
void CanOpen_Nmt_InitCompleteCallback();
void CanOpen_Nmt_OnResetCallback();
