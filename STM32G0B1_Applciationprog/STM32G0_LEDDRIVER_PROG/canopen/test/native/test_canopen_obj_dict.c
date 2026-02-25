#include "canopen_obj_dict.h"
#include "canopen_sdo.h"
#include "unity.h"

uint32_t empty_read_callback(ObjDict_Data* dst, const uint8_t subindex) { return 0; }
uint32_t empty_write_callback(const uint8_t subindex, const ObjDict_Data value) { return 0; }

void setUp() { ObjDict_Reset(); }

void tearDown() { }

// TODO: TESTING TIME
static ObjEntry param_entry = {
    .index_ = 0x6010,
    .num_subindices_ = 3,
    .permissions_ = OBJENTRY_PERMIT_R,
    .data_size_ = OBJENTRY_SIZE_BYTE,
    .read_callback_ = empty_read_callback,
    .write_callback_ = NULL,
};

static ObjEntry reboot_entry = {
    .index_ = 0x2ff0,
    .num_subindices_ = 0,
    .permissions_ = OBJENTRY_PERMIT_W,
    .data_size_ = OBJENTRY_SIZE_SHORT,
    .read_callback_ = empty_read_callback,
    .write_callback_ = empty_write_callback,
};

size_t AddEntryFromObj(const ObjEntry* entry) {
    return ObjDict_CreateEntry(entry->index_, entry->num_subindices_, entry->permissions_, entry->data_size_,
        entry->read_callback_, entry->write_callback_);
}

void setup_entries() {
    AddEntryFromObj(&param_entry);
    AddEntryFromObj(&reboot_entry);
}

void test_obj_dict_add_entry() {
    ObjEntry entry = {
        .index_ = 0x6010,
        .num_subindices_ = 3,
        .permissions_ = OBJENTRY_PERMIT_R,
        .data_size_ = OBJENTRY_SIZE_BYTE,
        .read_callback_ = empty_read_callback,
        .write_callback_ = empty_write_callback,
    };
    size_t curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(1, curr_size, "Wrong size of obj dict");
    entry.index_ = 0x2ff0;
    curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(2, curr_size, "Size of obj dict should be 2 after adding another entry");
}

void test_repeat_add_entry_does_nothing() {
    ObjEntry entry = {
        .index_ = 0x6010,
        .num_subindices_ = 3,
        .permissions_ = OBJENTRY_PERMIT_R,
        .data_size_ = OBJENTRY_SIZE_BYTE,
        .read_callback_ = empty_read_callback,
        .write_callback_ = empty_write_callback,
    };
    size_t curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(1, curr_size, "Wrong size of obj dict");
    curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(1, curr_size, "Obj dict should not have duplicated entries");
    entry.index_ = 0x2ff0;
    curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(2, curr_size, "Size of obj dict should be 2 after adding another entry");
}

void test_null_callback_does_nothing() {
    ObjEntry entry = {
        .index_ = 0x6010,
        .num_subindices_ = 3,
        .permissions_ = OBJENTRY_PERMIT_R,
        .data_size_ = OBJENTRY_SIZE_BYTE,
        .read_callback_ = NULL,
        .write_callback_ = NULL,
    };
    size_t curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(0, curr_size, "Validator should reject this entry");
    entry.permissions_ = OBJENTRY_PERMIT_W;
    curr_size = AddEntryFromObj(&entry);
    TEST_ASSERT_EQUAL_MESSAGE(0, curr_size, "Validator should reject this entry");
    ObjEntry new_entry = {
        .index_ = 0x2ff0,
        .num_subindices_ = 0,
        .permissions_ = OBJENTRY_PERMIT_R,
        .data_size_ = OBJENTRY_SIZE_SHORT,
        .read_callback_ = empty_read_callback,
        .write_callback_ = NULL,
    };
    curr_size = AddEntryFromObj(&new_entry);
    TEST_ASSERT_EQUAL_MESSAGE(1, curr_size, "new entry should be added");
}

void test_handle_sdo_write() {
    setup_entries();
    CanOpen_Frame req_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, 0x2ff0, 0, 2, 0xff00);
    CanOpen_Frame response = ObjDict_HandleSdo(&req_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(0x2ff060, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_MESSAGE(0xff00, response.data_.high_word_, "wrong high word");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
}

uint32_t handle_sdo_read_test_callback(ObjDict_Data* dst, const uint8_t subindex) {
    dst->u32 = 10;
    return 0;
}

void test_handle_sdo_read() {
    setup_entries();
    ObjEntry modded_entry = param_entry;
    modded_entry.read_callback_ = handle_sdo_read_test_callback;
    CanOpen_Frame req_frame = CanOpen_Sdo_CreateReadRequestFrame(0x20, 0x6010, 2);
    AddEntryFromObj(&modded_entry);
    CanOpen_Frame response = ObjDict_HandleSdo(&req_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(
        (param_entry.index_ << 8) | 0x4f | (2 << 24), response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(10, response.data_.high_word_, "wrong high word");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
}

void test_handle_sdo_write_fail() {
    setup_entries();
    CanOpen_Frame request_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, param_entry.index_, 2, 1, 0xf);
    CanOpen_Frame response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(
        (param_entry.index_ << 8) | 0x80 | (2 << 24), response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_NO_WRITE_PERM, response.data_.high_word_, "wrong high word");
}

void test_handle_sdo_read_fail_no_subindex() {
    setup_entries();
    const uint8_t subindex = 5;
    CanOpen_Frame request_frame = CanOpen_Sdo_CreateReadRequestFrame(0x20, param_entry.index_, subindex);
    CanOpen_Frame response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(
        (param_entry.index_ << 8) | 0x80 | (subindex << 24), response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_NO_SUBINDEX, response.data_.high_word_, "wrong high word");
}

uint32_t sdo_callback_error_read_callback(ObjDict_Data* obj, const uint8_t subindex) {
    return CANOPEN_SDO_ERR_INVALID_VALUE;
}

uint32_t sdo_callback_error_write_callback(const uint8_t subindex, const ObjDict_Data data) {
    return CANOPEN_SDO_ERR_INVALID_VALUE;
}

void test_handle_sdo_callback_error() {
    setup_entries();
    ObjEntry test_entry = { .index_ = 0x6011,
        .num_subindices_ = 0,
        .permissions_ = OBJENTRY_PERMIT_W | OBJENTRY_PERMIT_R,
        .data_size_ = OBJENTRY_SIZE_SHORT,
        .read_callback_ = sdo_callback_error_read_callback,
        .write_callback_ = sdo_callback_error_write_callback };
    AddEntryFromObj(&test_entry);
    CanOpen_Frame request_frame = CanOpen_Sdo_CreateReadRequestFrame(0x20, test_entry.index_, 0);
    CanOpen_Frame response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE((test_entry.index_ << 8) | 0x80, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_INVALID_VALUE, response.data_.high_word_, "wrong high word");

    request_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, test_entry.index_, 0, 2, 0xff);
    response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE((test_entry.index_ << 8) | 0x80, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_INVALID_VALUE, response.data_.high_word_, "wrong high word");
}

void test_handle_sdo_bad_datalen() {
    setup_entries();
    CanOpen_Frame request_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, reboot_entry.index_, 0, 4, 0xffffff);
    CanOpen_Frame response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE((reboot_entry.index_ << 8) | 0x80, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_LEN_NO_MATCH, response.data_.high_word_, "wrong high word");

    // write 0 bytes
    request_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, reboot_entry.index_, 0, 4, 0xffffff);
    response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE((reboot_entry.index_ << 8) | 0x80, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_LEN_NO_MATCH, response.data_.high_word_, "wrong high word");
}

void test_handle_sdo_not_found() {
    setup_entries();
    CanOpen_Frame request_frame = CanOpen_Sdo_CreateWriteRequestFrame(0x20, 0x1234, 0, 4, 0xffffff);
    CanOpen_Frame response = ObjDict_HandleSdo(&request_frame);
    TEST_ASSERT_EQUAL_MESSAGE(0x5A0, response.node_id_ + response.message_type_, "Wrong frame id");
    TEST_ASSERT_EQUAL_MESSAGE(8, response.length_, "wrong length");
    TEST_ASSERT_EQUAL_HEX_MESSAGE((0x1234 << 8) | 0x80, response.data_.low_word_, "wrong low word");
    TEST_ASSERT_EQUAL_HEX_MESSAGE(CANOPEN_SDO_ERR_NO_OBJ, response.data_.high_word_, "wrong high word");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_obj_dict_add_entry);
    RUN_TEST(test_repeat_add_entry_does_nothing);
    RUN_TEST(test_null_callback_does_nothing);
    RUN_TEST(test_handle_sdo_write);
    RUN_TEST(test_handle_sdo_read);
    RUN_TEST(test_handle_sdo_write_fail);
    RUN_TEST(test_handle_sdo_read_fail_no_subindex);
    RUN_TEST(test_handle_sdo_callback_error);
    RUN_TEST(test_handle_sdo_bad_datalen);
    RUN_TEST(test_handle_sdo_not_found);
    UNITY_END();
}
