#include "unity.h"
#include "canopen_heartbeat.h"

void setUp(void) {}
void tearDown(void) {}

void CanOpenHeartbeatNativeTest_test_start_state(void) {
    CanOpen_Frame frame = CanOpen_Heartbeat_CreateStartStateFrame(1);

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT, frame.message_type_, "Wrong message type. Should be CANOPEN_HEARTBEAT");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT_START, frame.data_.low_word_ & 0xFF, "Wrong data_low. Should be CANOPEN_HEARTBEAT_START");
}

void CanOpenHeartbeatNativeTest_test_stop_state(void) {
    CanOpen_Frame frame = CanOpen_Heartbeat_CreateStopStateFrame(1);

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT, frame.message_type_, "Wrong message type. Should be CANOPEN_HEARTBEAT");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT_STOP, frame.data_.low_word_ & 0xFF, "Wrong data_low. Should be CANOPEN_HEARTBEAT_STOP");
}

void CanOpenHeartbeatNativeTest_test_run_state(void) {
    CanOpen_Frame frame = CanOpen_Heartbeat_CreateRunStateFrame(1);

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT, frame.message_type_, "Wrong message type. Should be CANOPEN_HEARTBEAT");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT_RUN, frame.data_.low_word_ & 0xFF, "Wrong data_low. Should be CANOPEN_HEARTBEAT_RUN");
}

void CanOpenHeartbeatNativeTest_test_preop_state(void) {
    CanOpen_Frame frame = CanOpen_Heartbeat_CreatePreOpStateFrame(1);

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT, frame.message_type_, "Wrong message type. Should be CANOPEN_HEARTBEAT");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_HEARTBEAT_PREOPERATIONAL, frame.data_.low_word_ & 0xFF, "Wrong data_low. Should be CANOPEN_HEARTBEAT_PREOPERATIONAL");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(CanOpenHeartbeatNativeTest_test_start_state);
    RUN_TEST(CanOpenHeartbeatNativeTest_test_stop_state);
    RUN_TEST(CanOpenHeartbeatNativeTest_test_run_state);
    RUN_TEST(CanOpenHeartbeatNativeTest_test_preop_state);

    UNITY_END();
}
