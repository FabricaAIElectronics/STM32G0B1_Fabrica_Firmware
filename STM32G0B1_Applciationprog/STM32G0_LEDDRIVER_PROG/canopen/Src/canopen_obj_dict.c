#include "canopen_obj_dict.h"
#include <string.h>

typedef struct {
    ObjEntry entries_[OBJDICT_MAX_ENTRIES];
    size_t size_;
} ObjDict;

static ObjDict obj_dictionary;

size_t ObjDict_CreateEntry(uint16_t index, uint8_t num_subindices, ObjEntryPermissions permissions,
    ObjEntrySizeBytes size, uint32_t (*read_callback)(ObjDict_Data*, const uint8_t),
    uint32_t (*write_callback)(const uint8_t, const ObjDict_Data)) {
    if (obj_dictionary.size_ == OBJDICT_MAX_ENTRIES) {
        // TODO: call error or something
        return 0;
    }

    ObjEntry entry = { .index_ = index,
        .num_subindices_ = num_subindices,
        .permissions_ = permissions,
        .data_size_ = size,
        .read_callback_ = read_callback,
        .write_callback_ = write_callback };
    if ((permissions & OBJENTRY_PERMIT_R) == 0) {
        // Don't give a read callback if read not allowed
        entry.read_callback_ = NULL;
    } else if (!read_callback) {
        // TODO: call error or something
        return 0;
    }
    if ((permissions & OBJENTRY_PERMIT_W) == 0) {
        entry.write_callback_ = NULL;
    } else if (!write_callback) {
        // TODO: call error or something
        return 0;
    }
    for (size_t i = 0; i < obj_dictionary.size_; ++i) {
        // linear search for the entry and overwrite it
        if (obj_dictionary.entries_[i].index_ == entry.index_) {
            obj_dictionary.entries_[i] = entry;
            return obj_dictionary.size_;
        }
    }
    obj_dictionary.entries_[obj_dictionary.size_++] = entry;
    return obj_dictionary.size_;
}

void ObjDict_Reset() { memset(&obj_dictionary, 0, sizeof(obj_dictionary)); }

/*
 * @brief verifies the entry permissions given the command to read/write
 * @param cmd: sdo command (LSB of low word in data)
 * @param entry: pointer to object entry in question
 * @return permission that is granted to access, 0 if failed
 */
int __VerifyEntryPermissions(CanOpen_Frame* response, const CanOpen_Frame* frame, const ObjEntry* entry) {
    const uint8_t cmd = frame->data_.low_word_ & 0xff;
    if (cmd & CANOPEN_SDO_CCS_READ) {
        if (entry->permissions_ & OBJENTRY_PERMIT_R) {
            return OBJENTRY_PERMIT_R;
        }
        // Attempted to read an object that is not readable
        *response = CanOpen_Sdo_CreateErrorResponseFrame(
            frame->node_id_, entry->index_, CanOpen_Sdo_GetSubIndex(frame), CANOPEN_SDO_ERR_NO_READ_PERM);
        return 0;
    } else if (cmd & CANOPEN_SDO_CCS_WRITE) {
        if (entry->permissions_ & OBJENTRY_PERMIT_W) {
            return OBJENTRY_PERMIT_W;
        }
        // Attempted to write to an object that is not writable
        *response = CanOpen_Sdo_CreateErrorResponseFrame(
            frame->node_id_, entry->index_, CanOpen_Sdo_GetSubIndex(frame), CANOPEN_SDO_ERR_NO_WRITE_PERM);
        return 0;
    }
    // Malformed command, no request to read or write
    *response = CanOpen_Sdo_CreateErrorResponseFrame(
        frame->node_id_, entry->index_, CanOpen_Sdo_GetSubIndex(frame), CANOPEN_SDO_ERR_BAD_COMMAND);
    return 0;
}

int __SubindexCheck(CanOpen_Frame* response, const CanOpen_Frame* frame, const ObjEntry* entry) {
    // Check that the subindex falls within range
    const uint8_t subindex = CanOpen_Sdo_GetSubIndex(frame);
    if (subindex > entry->num_subindices_) {
        *response = CanOpen_Sdo_CreateErrorResponseFrame(
            frame->node_id_, entry->index_, subindex, CANOPEN_SDO_ERR_NO_SUBINDEX);
        return 0;
    }
    return 1;
}

int __WriteDataCheck(CanOpen_Frame* response, const CanOpen_Frame* frame, const ObjEntry* entry) {
    // Check that the data length to be written <= size of object
    const uint8_t write_len = CanOpen_Sdo_GetDataLenBytes(frame);
    if ((write_len == 0) || (write_len > entry->data_size_)) {
        *response = CanOpen_Sdo_CreateErrorResponseFrame(
            frame->node_id_, entry->index_, CanOpen_Sdo_GetSubIndex(frame), CANOPEN_SDO_ERR_LEN_NO_MATCH);
        return 0;
    }
    return 1;
}

/*
 * @brief performs all the verification checks for processing
 * @return access if the verification passes, 0 if fail. reason will be in the response frame
 */

int __VerifyFrame(CanOpen_Frame* response, const CanOpen_Frame* frame, const ObjEntry* entry) {
    const int access = __VerifyEntryPermissions(response, frame, entry);
    if (!access) {
        return 0;
    }
    if (!__SubindexCheck(response, frame, entry)) {
        return 0;
    }
    if (access == OBJENTRY_PERMIT_W) {
        if (!__WriteDataCheck(response, frame, entry)) {
            return 0;
        }
    }
    return access;
}

void __ProcessFrameByEntry(CanOpen_Frame* response, const CanOpen_Frame* frame, const ObjEntry* entry) {
    const int access = __VerifyFrame(response, frame, entry);
    if (!access) {
        return;
    }

    // Process the frame given the entry
    uint32_t error_id = 0;
    ObjDict_Data data = { 0 };
    const uint8_t subindex = CanOpen_Sdo_GetSubIndex(frame);
    switch (access) {
    case OBJENTRY_PERMIT_W: {
        data.u32 = frame->data_.high_word_;
        error_id = entry->write_callback_(subindex, data);
        if (error_id) {
            *response = CanOpen_Sdo_CreateErrorResponseFrame(frame->node_id_, entry->index_, subindex, error_id);
            break;
        }
        *response = CanOpen_Sdo_CreateWriteAckResponseFrame(
            frame->node_id_, entry->index_, subindex, frame->data_.high_word_);
        break;
    }
    case OBJENTRY_PERMIT_R: {
        if ((subindex == 0) && entry->num_subindices_) {
            // If there are subindices, then subindex 0 is the total number of subindices in this object
            *response = CanOpen_Sdo_CreateReadDataResponseFrame(
                frame->node_id_, entry->index_, subindex, entry->data_size_, entry->num_subindices_);
            break;
        }
        error_id = entry->read_callback_(&data, subindex);
        if (error_id) {
            *response = CanOpen_Sdo_CreateErrorResponseFrame(frame->node_id_, entry->index_, subindex, error_id);
            break;
        }
        *response = CanOpen_Sdo_CreateReadDataResponseFrame(
            frame->node_id_, entry->index_, subindex, entry->data_size_, data.u32);
        break;
    }
    }
}

CanOpen_Frame ObjDict_HandleSdo(const CanOpen_Frame* frame) {
    // find the entry in the obj dict (linear search for now is fine)
    // verify permissions
    // call on_read_ or on_write_ or generate the frame from the pointer
    CanOpen_Frame response_frame = { 0 };

    // compare the index and data len, if there is a match then process it
    const uint16_t frame_index = CanOpen_Sdo_GetIndex(frame);
    for (size_t i = 0; i < OBJDICT_MAX_ENTRIES; ++i) {
        const uint16_t entry_index = obj_dictionary.entries_[i].index_;
        if (entry_index == frame_index) {

            __ProcessFrameByEntry(&response_frame, frame, &obj_dictionary.entries_[i]);
            return response_frame;
        }
    }

    // No object found
    response_frame = CanOpen_Sdo_CreateErrorResponseFrame(
        frame->node_id_, frame_index, CanOpen_Sdo_GetSubIndex(frame), CANOPEN_SDO_ERR_NO_OBJ);
    return response_frame;
}
