#include "canopen_buffer.h"
#include "unity.h"
#include <string.h>

void setUp() { }
void tearDown() { }

void test_tx_queue() {
    CanRingBuffer test_buffer = RingBuffer_Create();
    // enqueue stuff
    CanOpen_Frame frame;
    for (size_t i = 0; i < test_buffer.capacity_; ++i) {
        frame.node_id_ = i;
        frame.message_type_ = 0;
        frame.length_ = 8;
        const uint32_t test_data = i | i << 8 | i << 16 | i << 24;
        frame.data_.low_word_ = test_data;
        frame.data_.high_word_ = test_data;
        TEST_ASSERT_TRUE_MESSAGE(
            RingBuffer_EnqueueCanOpen(&test_buffer, &frame), "Buffer should allow enqueueing when size < capacity");
        TEST_ASSERT_EQUAL_MESSAGE(i + 1, RingBuffer_GetSize(&test_buffer), "Wrong size of buffer");
        TEST_ASSERT_EQUAL_MESSAGE((i + 1) & 0x7, test_buffer.write_idx_, "Wrong write index");
        TEST_ASSERT_EQUAL_MESSAGE(0, test_buffer.read_idx_, "Wrong read index");
    }
    TEST_ASSERT_FALSE_MESSAGE(
        RingBuffer_EnqueueCanOpen(&test_buffer, &frame), "Buffer is full and should reject enqueue");

    // dequeue stuff
    STMCanFrame stm_frame;
    for (size_t i = 0; i < test_buffer.capacity_; ++i) {
        const uint32_t test_data = i | i << 8 | i << 16 | i << 24;
        TEST_ASSERT_TRUE_MESSAGE(
            RingBuffer_DequeueSTM(&test_buffer, &stm_frame), "Buffer should allow dequeueing when size > 0");
        TEST_ASSERT_EQUAL_MESSAGE((i + 1) & 0x7, test_buffer.read_idx_, "Wrong read index");
        TEST_ASSERT_EQUAL_MESSAGE(7, test_buffer.write_idx_, "Wrong write index");
        TEST_ASSERT_EQUAL_MESSAGE(i, stm_frame.header_.id_, "Wrong stdid");
        TEST_ASSERT_EQUAL_MESSAGE(8, stm_frame.header_.data_len_, "Wrong DLC");
        TEST_ASSERT_EQUAL_MESSAGE(test_data, stm_frame.data_.words.low, "Wrong low word data");
        TEST_ASSERT_EQUAL_MESSAGE(test_data, stm_frame.data_.words.high, "Wrong high word data");
    }
    TEST_ASSERT_FALSE_MESSAGE(RingBuffer_DequeueSTM(&test_buffer, &stm_frame), "Buffer should reject dequeue");
}

void test_rx_queue() {
    CanRingBuffer test_buffer = RingBuffer_Create();
    // enqueue stuff
    STMCanFrame stm_frame = { 0 };
    CanOpen_Frame frame = { 0 };
    for (size_t i = 0; i < test_buffer.capacity_; ++i) {
        stm_frame.header_.data_len_ = 8;
        stm_frame.header_.id_ = i;
        const uint32_t test_data = i | i << 8 | i << 16 | i << 24;
        stm_frame.data_.words.high = test_data;
        stm_frame.data_.words.low = test_data;
        TEST_ASSERT_TRUE_MESSAGE(
            RingBuffer_EnqueueSTM(&test_buffer, &stm_frame), "Buffer should allow enqueueing when size < capacity");
        TEST_ASSERT_EQUAL_MESSAGE(i + 1, RingBuffer_GetSize(&test_buffer), "Wrong size of buffer");
        TEST_ASSERT_EQUAL_MESSAGE((i + 1) & 0x7, test_buffer.write_idx_, "Wrong write index");
        TEST_ASSERT_EQUAL_MESSAGE(0, test_buffer.read_idx_, "Wrong read index");
    }
    TEST_ASSERT_FALSE_MESSAGE(
        RingBuffer_EnqueueSTM(&test_buffer, &stm_frame), "Buffer is full and should reject enqueue");

    // dequeue stuff
    for (size_t i = 0; i < test_buffer.capacity_; ++i) {
        const uint32_t test_data = i | i << 8 | i << 16 | i << 24;

        TEST_ASSERT_TRUE_MESSAGE(
            RingBuffer_DequeueCanOpen(&test_buffer, &frame), "Buffer should allow dequeueing when size > 0");
        TEST_ASSERT_EQUAL_MESSAGE((i + 1) & 0x7, test_buffer.read_idx_, "Wrong read index");
        TEST_ASSERT_EQUAL_MESSAGE(7, test_buffer.write_idx_, "Wrong write index");
        TEST_ASSERT_EQUAL_MESSAGE(i, frame.node_id_, "Wrong node id");
        TEST_ASSERT_EQUAL_MESSAGE(8, frame.length_, "Wrong frame length");
        TEST_ASSERT_EQUAL_MESSAGE(test_data, frame.data_.low_word_, "Wrong low word data");
        TEST_ASSERT_EQUAL_MESSAGE(test_data, frame.data_.high_word_, "Wrong high word data");
    }
    TEST_ASSERT_FALSE_MESSAGE(RingBuffer_DequeueCanOpen(&test_buffer, &frame), "Buffer should reject dequeue");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tx_queue);
    RUN_TEST(test_rx_queue);
    UNITY_END();
    return 0;
}
