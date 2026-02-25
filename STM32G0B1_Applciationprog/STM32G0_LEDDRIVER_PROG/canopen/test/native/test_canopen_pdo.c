#include "canopen_pdo.h"
#include "unity.h"

void setUp(void) { }
void tearDown(void) { }

void CanOpenPdoNativeTest_test_create_out_frame(void) {

    uint8_t node_id = 1;

    uint32_t rpdo_id = 0x200;
    uint8_t data_len = 8;
    CanOpen_FrameData rpdo_data = { 0x12345678, 0x87654321 };

    CanOpen_Frame rpdo_frame = CanOpen_Pdo_CreateOutFrame(node_id, rpdo_id, data_len, rpdo_data);

    TEST_ASSERT_EQUAL_MESSAGE(node_id, rpdo_frame.node_id_, "Wrong node_id. Should be 1");
    TEST_ASSERT_EQUAL_MESSAGE(data_len, rpdo_frame.length_, "Wrong frame length. Should be 8");
    TEST_ASSERT_EQUAL_MESSAGE(rpdo_data.low_word_, rpdo_frame.data_.low_word_, "Wrong low_word. Should be 0x12345678");
    TEST_ASSERT_EQUAL_MESSAGE(
        rpdo_data.high_word_, rpdo_frame.data_.high_word_, "Wrong high_word. Should be 0x87654321");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(CanOpenPdoNativeTest_test_create_out_frame);
    UNITY_END();
}
