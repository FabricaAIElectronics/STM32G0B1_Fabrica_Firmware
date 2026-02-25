#include "canopen_nmt.h"
#include "unity.h"
#include <stdint.h>

int on_reset_called = 0;
int init_complete_called = 0;

void setUp(void) {
    init_complete_called = 0;
    on_reset_called = 0;
    CanOpen_Nmt_SetState(CANOPEN_NMT_INVALID_COMMAND);
}
void tearDown(void) { }

#define TEST_NODE_ID 2
#define NODE_ID_POSITION CanOpen_Nmt_NodeIdPosition

static void CanOpenNmtNativeTest_CheckNmtFrame(const CanOpen_Frame* frame, CanOpen_Nmt_Command expected_command) {
    TEST_ASSERT_EQUAL_MESSAGE(
        TEST_NODE_ID, (frame->data_.low_word_ >> CanOpen_Nmt_NodeIdPosition) & 0xFF, "Wrong Node id");
    TEST_ASSERT_EQUAL_MESSAGE(CanOpen_Nmt_ControlDataLength, frame->length_, "Wrong message length. Should be 2");
    TEST_ASSERT_EQUAL_MESSAGE(
        ((uint32_t)expected_command & 0xFF) | ((uint32_t)TEST_NODE_ID << CanOpen_Nmt_NodeIdPosition),
        frame->data_.low_word_, "Wrong data_low");
    TEST_ASSERT_EQUAL_MESSAGE(0, frame->data_.high_word_, "Wrong data_high");
}

void test_nmt_sanity() {
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_NMT_INVALID_COMMAND, CanOpen_Nmt_GetState(), "Wrong state");
}

void test_stay_in_init_state() {
    CanOpen_Nmt_Command state = CanOpen_Nmt_GetState();

    // init state -> should not do anything

    CanOpen_Frame frame = CanOpen_Nmt_CreateStartNodeFrame(1);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_MESSAGE(state, CanOpen_Nmt_GetState(), "Should not go out of init state");

    frame = CanOpen_Nmt_CreateStopFrame(1);
    state = CANOPEN_NMT_RESET_COMMUNICATION;
    CanOpen_Nmt_SetState(state);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_MESSAGE(state, CanOpen_Nmt_GetState(), "Should not go out of init state");

    frame = CanOpen_Nmt_CreatePreOpFrame(1);
    state = CANOPEN_NMT_RESET_NODE;
    CanOpen_Nmt_SetState(state);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_MESSAGE(state, CanOpen_Nmt_GetState(), "Should not go out of init state");
}

void CanOpenNmtNativeTest_test_control_open_node() {
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
    CanOpen_Frame frame = CanOpen_Nmt_CreateStartNodeFrame(TEST_NODE_ID);
    CanOpenNmtNativeTest_CheckNmtFrame(&frame, CANOPEN_NMT_OPERATIONAL);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(
        CANOPEN_NMT_OPERATIONAL, CanOpen_Nmt_GetState(), "Should have gone to operational state");
}

void CanOpenNmtNativeTest_test_control_end_node() {
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
    CanOpen_Frame frame = CanOpen_Nmt_CreateStopFrame(TEST_NODE_ID);
    CanOpenNmtNativeTest_CheckNmtFrame(&frame, CANOPEN_NMT_STOP);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(CANOPEN_NMT_STOP, CanOpen_Nmt_GetState(), "Should have gone to stop state");
}

void CanOpenNmtNativeTest_test_control_preop() {
    CanOpen_Nmt_SetState(CANOPEN_NMT_OPERATIONAL);
    CanOpen_Frame frame = CanOpen_Nmt_CreatePreOpFrame(TEST_NODE_ID);
    CanOpenNmtNativeTest_CheckNmtFrame(&frame, CANOPEN_NMT_PREOPERATIONAL);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(
        CANOPEN_NMT_PREOPERATIONAL, CanOpen_Nmt_GetState(), "Should have gone to preoperational state");
}

void CanOpenNmtNativeTest_test_control_reset_node() {
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
    CanOpen_Frame frame = CanOpen_Nmt_CreateResetNodeFrame(TEST_NODE_ID);
    CanOpenNmtNativeTest_CheckNmtFrame(&frame, CANOPEN_NMT_RESET_NODE);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(
        CANOPEN_NMT_RESET_NODE, CanOpen_Nmt_GetState(), "Should have gone to reset node state");
}
void CanOpenNmtNativeTest_test_control_reset_comms() {
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
    CanOpen_Frame frame = CanOpen_Nmt_CreateResetCommunicationsFrame(TEST_NODE_ID);
    CanOpenNmtNativeTest_CheckNmtFrame(&frame, CANOPEN_NMT_RESET_COMMUNICATION);
    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(
        CANOPEN_NMT_RESET_COMMUNICATION, CanOpen_Nmt_GetState(), "Should have gone to reset communication state");
}

void CanOpen_Nmt_InitCompleteCallback() {
    // This should be called if the init is complete
    init_complete_called++;
}

void CanOpen_Nmt_OnResetCallback() {
    // This should be called on reception of a reset packet
    on_reset_called++;
}

void test_nmt_reset_sequence() {
    CanOpen_Frame frame = { 0 };
    frame.length_ = 2;
    frame.node_id_ = 1;
    frame.data_.low_word_ = CANOPEN_NMT_RESET_NODE | (frame.node_id_ << 8);
    frame.message_type_ = CANOPEN_NMT_CONTROL;
    CanOpen_Nmt_SetState(CANOPEN_NMT_OPERATIONAL);

    CanOpen_Nmt_ProcessFrame(&frame);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, on_reset_called, "OnResetCallback not called on reset frame.");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_NMT_RESET_NODE, CanOpen_Nmt_GetState(), "NMT not in reset state");
    CanOpen_Nmt_SetState(CANOPEN_NMT_PREOPERATIONAL);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        0, init_complete_called, "InitCompleteCallback not called on reset -> preoperational transition.");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_NMT_PREOPERATIONAL, CanOpen_Nmt_GetState(), "NMT not in reset state");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(CanOpenNmtNativeTest_test_control_open_node);
    RUN_TEST(CanOpenNmtNativeTest_test_control_end_node);
    RUN_TEST(CanOpenNmtNativeTest_test_control_preop);
    RUN_TEST(CanOpenNmtNativeTest_test_control_reset_node);
    RUN_TEST(CanOpenNmtNativeTest_test_control_reset_comms);
    RUN_TEST(test_nmt_sanity);
    RUN_TEST(test_stay_in_init_state);
    RUN_TEST(test_nmt_reset_sequence);
    UNITY_END();
}
