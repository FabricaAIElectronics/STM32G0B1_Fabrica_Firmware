#include "canopen_frame.h"
#include "unity.h"

void setUp(void) { }
void tearDown(void) { }

void CanOpenNativeTest_test_create_frame(void) {
    CanOpen_Frame frame;
    CanOpen_FrameData data;

    data.low_word_ = 0x11223344;
    data.high_word_ = 0x55667788;

    CanOpen_Frame_CreateFrame(&frame, 1, CANOPEN_PDO_IN1, 8, &data, 123);

    TEST_ASSERT_EQUAL_MESSAGE(1, frame.node_id_, "Wrong node_id. Should be 1");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_PDO_IN1, frame.message_type_, "Wrong message_type. Should be CANOPEN_PDO_IN1");
    TEST_ASSERT_EQUAL_MESSAGE(8, frame.length_, "Wrong frame length. Should be 8");
    TEST_ASSERT_EQUAL_MESSAGE(0x11223344, frame.data_.low_word_, "Wrong low_word. Should be 0x11223344");
    TEST_ASSERT_EQUAL_MESSAGE(0x55667788, frame.data_.high_word_, "Wrong high_word. Should be 0x55667788");
    TEST_ASSERT_EQUAL_MESSAGE(123, frame.timestamp_, "Wrong timestamp. Should be 123");
}

void CanOpenNativeTest_test_create_empty_frame(void) {
    CanOpen_Frame frame;

    CanOpen_Frame_CreateEmptyFrame(&frame);

    TEST_ASSERT_EQUAL_MESSAGE(0, frame.node_id_, "Wrong node_id. Should be 0");
    TEST_ASSERT_EQUAL_MESSAGE(
        CANOPEN_INVALID_ID, frame.message_type_, "Wrong message_type. Should be CANOPEN_INVALID_ID");
    TEST_ASSERT_EQUAL_MESSAGE(0, frame.length_, "Wrong frame length. Should be 0");
    TEST_ASSERT_EQUAL_MESSAGE(0, frame.data_.low_word_, "Wrong low_word. Should be 0");
    TEST_ASSERT_EQUAL_MESSAGE(0, frame.data_.high_word_, "Wrong high_word. Should be 0");
    TEST_ASSERT_EQUAL_MESSAGE(0, frame.timestamp_, "Wrong timestamp. Should be 0");
}

void CanOpenNativeTest_test_frame_overwrite(void) {
    CanOpen_Frame frame;
    CanOpen_FrameData data;

    data.low_word_ = 0xAAAAAAAA;
    data.high_word_ = 0xBBBBBBBB;

    CanOpen_Frame_CreateEmptyFrame(&frame);

    CanOpen_Frame_Overwrite(&frame, 2, CANOPEN_PDO_OUT1, 4, &data, 999);

    TEST_ASSERT_EQUAL_MESSAGE(2, frame.node_id_, "Wrong node_id. Should be 2");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_PDO_OUT1, frame.message_type_, "Wrong message_type. Should be CANOPEN_PDO_OUT1");
    TEST_ASSERT_EQUAL_MESSAGE(4, frame.length_, "Wrong frame length. Should be 4");
    TEST_ASSERT_EQUAL_MESSAGE(0xAAAAAAAA, frame.data_.low_word_, "Wrong low_word. Should be 0xAAAAAAAA");
    TEST_ASSERT_EQUAL_MESSAGE(0xBBBBBBBB, frame.data_.high_word_, "Wrong high_word. Should be 0xBBBBBBBB");
    TEST_ASSERT_EQUAL_MESSAGE(999, frame.timestamp_, "Wrong timestamp. Should be 999");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(CanOpenNativeTest_test_create_frame);
    RUN_TEST(CanOpenNativeTest_test_create_empty_frame);
    RUN_TEST(CanOpenNativeTest_test_frame_overwrite);

    UNITY_END();
}
