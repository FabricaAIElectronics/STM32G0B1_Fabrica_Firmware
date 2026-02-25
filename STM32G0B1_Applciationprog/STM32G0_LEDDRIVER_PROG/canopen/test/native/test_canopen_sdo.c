#include "canopen_sdo.h"
#include "unity.h"
#include <stdbool.h>

void setUp(void) { }
void tearDown(void) { }

void CanOpenSdoNativeTest_test_read_request_frame(void) {
    CanOpen_Frame frame = CanOpen_Sdo_CreateReadRequestFrame(1, 0x6040, 0x00);

    uint8_t command = frame.data_.low_word_ & 0xFF;
    uint16_t index = (frame.data_.low_word_ >> 8) & 0xFFFF;
    uint8_t subindex = (frame.data_.low_word_ >> 24) & 0xFF;

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_REQUEST, frame.message_type_, "Wrong message_type. Should be 0x600");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_DATA_LENGTH, frame.length_, "Wrong data length. Should be 8");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_READ_PARAM, command, "Wrong command. Should be 0x40");
    TEST_ASSERT_EQUAL_MESSAGE(0x6040, index, "Wrong index. Should be 0x6040");
    TEST_ASSERT_EQUAL_MESSAGE(0x00, subindex, "Wrong subindex. Should be 0x00");
}

void CanOpenSdoNativeTest_test_write_request_frame(void) {
    uint32_t write_data = 0x11223344;
    uint8_t data_len = 4;
    CanOpen_Frame frame = CanOpen_Sdo_CreateWriteRequestFrame(1, 0x6040, 0x01, data_len, write_data);

    uint8_t command = frame.data_.low_word_ & 0xFF;
    uint16_t index = (frame.data_.low_word_ >> 8) & 0xFFFF;
    uint8_t subindex = (frame.data_.low_word_ >> 24) & 0xFF;

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_REQUEST, frame.message_type_, "Wrong message_type. Should be 0x600");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_DATA_LENGTH, frame.length_, "Wrong data length. Should be 8");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_WRITE_PARAM, command, "Wrong command. Should be 0x23");
    TEST_ASSERT_EQUAL_MESSAGE(0x6040, index, "Wrong index. Should be 0x6040");
    TEST_ASSERT_EQUAL_MESSAGE(0x01, subindex, "Wrong subindex. Should be 0x00");
    TEST_ASSERT_EQUAL_MESSAGE(write_data, frame.data_.high_word_, "Wrong wrtie_data. Should be 0x11223344");
}

void CanOpenSdoNativeTest_test_read_response_frame(void) {
    uint8_t node_id = 1;
    uint16_t index = 0x6040;
    uint8_t subindex = 0x00;
    uint32_t response_data = 0x55667788;
    uint8_t data_len = 4;

    CanOpen_Frame frame = CanOpen_Sdo_CreateReadDataResponseFrame(node_id, index, subindex, data_len, response_data);

    uint8_t command = frame.data_.low_word_ & 0xFF;
    uint16_t frame_index = (frame.data_.low_word_ >> 8) & 0xFFFF;
    uint8_t frame_subindex = (frame.data_.low_word_ >> 24) & 0xFF;

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_RESPONSE, frame.message_type_, "Wrong message_type. Should be 0x580");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_DATA_LENGTH, frame.length_, "Wrong data length. Should be 8");
    TEST_ASSERT_EQUAL_MESSAGE(
        CANOPEN_SDO_RECEIVE_PARAM_LONG, command, "Wrong command byte for 4-byte read response. Should be 0x43");
    TEST_ASSERT_EQUAL_MESSAGE(index, frame_index, "Wrong index. Should be 0x6040");
    TEST_ASSERT_EQUAL_MESSAGE(subindex, frame_subindex, "Wrong subinde. Should be 0x00");
    TEST_ASSERT_EQUAL_MESSAGE(response_data, frame.data_.high_word_, "Wrong response_data. Should be 0x55667788");
}

void CanOpenSdoNativeTest_test_write_response_frame(void) {
    uint8_t node_id = 1;
    uint16_t index = 0x6040;
    uint8_t subindex = 0x01;
    CanOpen_Frame ack_frame = CanOpen_Sdo_CreateWriteAckResponseFrame(node_id, index, subindex, 0);

    uint8_t ack_command = ack_frame.data_.low_word_ & 0xFF;
    uint16_t ack_index = (ack_frame.data_.low_word_ >> 8) & 0xFFFF;
    uint8_t ack_subindex = (ack_frame.data_.low_word_ >> 24) & 0xFF;

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_RESPONSE, ack_frame.message_type_, "Wrong message_type. Should be 0x580");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_ACK, ack_command, "Wrong command. Should be 0x60");
    TEST_ASSERT_EQUAL_MESSAGE(index, ack_index, "Wrong index. Should be 0x6040");
    TEST_ASSERT_EQUAL_MESSAGE(subindex, ack_subindex, "Wrong subindex. Should be 0x01");
    TEST_ASSERT_EQUAL_MESSAGE(0, ack_frame.data_.high_word_, "High word should be 0 for write response");

    CanOpen_Frame error_frame = CanOpen_Sdo_CreateErrorResponseFrame(node_id, index, subindex, 0);

    uint8_t error_command = error_frame.data_.low_word_ & 0xFF;
    uint16_t error_index = (error_frame.data_.low_word_ >> 8) & 0xFFFF;
    uint8_t error_subindex = (error_frame.data_.low_word_ >> 24) & 0xFF;

    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_RESPONSE, error_frame.message_type_, "Wrong message_type. Should be 0x580");
    TEST_ASSERT_EQUAL_MESSAGE(CANOPEN_SDO_ERROR, error_command, "Wrong command. Should be 0x80");
    TEST_ASSERT_EQUAL_MESSAGE(index, error_index, "Wrong index. Should be 0x6040");
    TEST_ASSERT_EQUAL_MESSAGE(subindex, error_subindex, "Wrong subindex. Should be 0x01");
    TEST_ASSERT_EQUAL_MESSAGE(0, error_frame.data_.high_word_, "High word should be 0 for write response");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(CanOpenSdoNativeTest_test_read_request_frame);
    RUN_TEST(CanOpenSdoNativeTest_test_write_request_frame);
    RUN_TEST(CanOpenSdoNativeTest_test_read_response_frame);
    RUN_TEST(CanOpenSdoNativeTest_test_write_response_frame);

    UNITY_END();
}
