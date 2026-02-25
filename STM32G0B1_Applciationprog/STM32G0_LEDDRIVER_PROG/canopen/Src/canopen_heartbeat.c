#include "canopen_heartbeat.h"


CanOpen_Frame CanOpen_Heartbeat_CreateHeartbeatFrame(uint8_t node_id, CanOpen_Heartbeat_Status status) {
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    /* Heartbeat data: just the status in the low byte */
    frame_data.low_word_ = (uint32_t)status;
    frame_data.high_word_ = 0;

    CanOpen_Frame_CreateFrame(&frame,
                              node_id,
                              CANOPEN_HEARTBEAT,
                              CanOpen_Heartbeat_HeartbeatDataLength,
                              &frame_data,
                              0);

    return frame;
}

/* Convenience functions for standard heartbeat states */
CanOpen_Frame CanOpen_Heartbeat_CreateStartStateFrame(uint8_t node_id) {
    return CanOpen_Heartbeat_CreateHeartbeatFrame(node_id, CANOPEN_HEARTBEAT_START);
}

CanOpen_Frame CanOpen_Heartbeat_CreateStopStateFrame(uint8_t node_id) {
    return CanOpen_Heartbeat_CreateHeartbeatFrame(node_id, CANOPEN_HEARTBEAT_STOP);
}

CanOpen_Frame CanOpen_Heartbeat_CreateRunStateFrame(uint8_t node_id) {
    return CanOpen_Heartbeat_CreateHeartbeatFrame(node_id, CANOPEN_HEARTBEAT_RUN);
}

CanOpen_Frame CanOpen_Heartbeat_CreatePreOpStateFrame(uint8_t node_id) {
    return CanOpen_Heartbeat_CreateHeartbeatFrame(node_id, CANOPEN_HEARTBEAT_PREOPERATIONAL);
}
