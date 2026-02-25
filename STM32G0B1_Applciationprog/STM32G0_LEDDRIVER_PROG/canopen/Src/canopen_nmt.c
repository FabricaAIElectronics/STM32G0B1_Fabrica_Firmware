#include "canopen_nmt.h"

static CanOpen_Nmt_Command state_ = CANOPEN_NMT_INVALID_COMMAND;

static CanOpen_Frame CanOpen_Nmt_CreateControlFrame(uint8_t node_id, CanOpen_Nmt_Command command) {
    CanOpen_Frame frame;
    CanOpen_FrameData frame_data;

    frame_data.low_word_ = ((uint32_t)command & 0xFF) | ((uint32_t)node_id << CanOpen_Nmt_NodeIdPosition);
    frame_data.high_word_ = 0;

    CanOpen_Frame_CreateFrame(&frame, 0, CanOpen_Nmt_ControlCobId, CanOpen_Nmt_ControlDataLength, &frame_data, 0);

    return frame;
}

void CanOpen_Nmt_SetState(CanOpen_Nmt_Command state) {
    if (state_ == CANOPEN_NMT_RESET_NODE || state_ == CANOPEN_NMT_RESET_COMMUNICATION
        || state_ == CANOPEN_NMT_INVALID_COMMAND) {
        if (state == CANOPEN_NMT_PREOPERATIONAL) {
            CanOpen_Nmt_InitCompleteCallback();
        }
    }
    state_ = state;
}

CanOpen_Nmt_Command CanOpen_Nmt_GetState() { return state_; }

void CanOpen_Nmt_ProcessFrame(CanOpen_Frame* frame) {
    if (frame->message_type_ != CANOPEN_NMT_CONTROL || state_ == CANOPEN_NMT_INVALID_COMMAND) {
        return;
    }

    const uint8_t command = frame->data_.low_word_ & 0xff;
    // If we are in a reset state, ignore everything except reset commands
    if (state_ == CANOPEN_NMT_RESET_NODE || state_ == CANOPEN_NMT_RESET_COMMUNICATION) {
        if (command != CANOPEN_NMT_RESET_NODE && command != CANOPEN_NMT_RESET_COMMUNICATION) {
            return;
        }
        state_ = command;
        CanOpen_Nmt_OnResetCallback();
        return;
    }

    // In all other states if we get a reset command, we should change state to it, except if we haven't initialized.
    if (command == CANOPEN_NMT_RESET_NODE || command == CANOPEN_NMT_RESET_COMMUNICATION) {
        state_ = command;
        CanOpen_Nmt_OnResetCallback();
    } else {
        state_ = command;
    }
}

CanOpen_Frame CanOpen_Nmt_CreateStartNodeFrame(uint8_t node_id) {
    return CanOpen_Nmt_CreateControlFrame(node_id, CANOPEN_NMT_OPERATIONAL);
}

CanOpen_Frame CanOpen_Nmt_CreateStopFrame(uint8_t node_id) {
    return CanOpen_Nmt_CreateControlFrame(node_id, CANOPEN_NMT_STOP);
}

CanOpen_Frame CanOpen_Nmt_CreatePreOpFrame(uint8_t node_id) {
    return CanOpen_Nmt_CreateControlFrame(node_id, CANOPEN_NMT_PREOPERATIONAL);
}

CanOpen_Frame CanOpen_Nmt_CreateResetNodeFrame(uint8_t node_id) {
    return CanOpen_Nmt_CreateControlFrame(node_id, CANOPEN_NMT_RESET_NODE);
}

CanOpen_Frame CanOpen_Nmt_CreateResetCommunicationsFrame(uint8_t node_id) {
    return CanOpen_Nmt_CreateControlFrame(node_id, CANOPEN_NMT_RESET_COMMUNICATION);
}

__attribute__((weak)) void CanOpen_Nmt_InitCompleteCallback() {
    // MUST send a heartbeat message in whatever function that overrides this
    // Raise not implemented error if it's not in a native test
#ifndef NATIVE_TEST
    extern void ERROR_CanOpen_Nmt_InitCompleteCallback_NotImplemented();
    ERROR_CanOpen_Nmt_InitCompleteCallback_NotImplemented();
#endif
}

__attribute__((weak)) void CanOpen_Nmt_OnResetCallback() {
    // MUST send a heartbeat message in whatever function that overrides this
    // Raise not implemented error if it's not in a native test
#ifndef NATIVE_TEST
    extern void ERROR_CanOpen_Nmt_OnResetCallback_NotImplemented();
    ERROR_CanOpen_Nmt_OnResetCallback_NotImplemented();
#endif
}
