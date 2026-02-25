#include "canopen_buffer.h"
#include <string.h>

void CanOpenToStmTx(const CanOpen_Frame* const src, STMCanFrame* dst) {
    dst->header_.data_len_ = src->length_;
    dst->header_.id_ = src->message_type_ + src->node_id_;
    dst->header_.is_ext_id_ = 0;
    dst->data_.words.low = src->data_.low_word_;
    dst->data_.words.high = src->data_.high_word_;
}

void StmRxToCanOpen(const STMCanFrame* const src, CanOpen_Frame* dst) {
    CanOpen_FrameData data = { .low_word_ = src->data_.words.low, .high_word_ = src->data_.words.high };
    CanOpen_Frame_CreateFrame(dst, src->header_.id_ & 0x7f, src->header_.id_ & 0x780, src->header_.data_len_, &data, 0);
}

CanRingBuffer RingBuffer_Create() {
    CanRingBuffer buf = { .write_idx_ = 0, .read_idx_ = 0, .capacity_ = 7 };
    memset(&(buf.data_), 0, sizeof(STMCanFrame) * 8);
    return buf;
}

void RingBuffer_Clear(CanRingBuffer* buf) {
    buf->write_idx_ = 0;
    buf->read_idx_ = 0;
}

int RingBuffer_EnqueueCanOpen(CanRingBuffer* buf, CanOpen_Frame* src) {
    if (((buf->read_idx_ - buf->write_idx_) & (buf->capacity_)) == 1) {
        return 0;
    }

    CanOpenToStmTx(src, &(buf->data_[buf->write_idx_]));
    buf->write_idx_++;
    buf->write_idx_ &= buf->capacity_;
    return 1;
}

int RingBuffer_DequeueCanOpen(CanRingBuffer* buf, CanOpen_Frame* dst) {
    // directly move the ring buffer data to canopen frame
    if (buf->read_idx_ == buf->write_idx_) {
        return 0;
    }

    StmRxToCanOpen(&(buf->data_[buf->read_idx_]), dst);
    buf->read_idx_++;
    buf->read_idx_ &= buf->capacity_;
    return 1;
}

int RingBuffer_EnqueueSTM(CanRingBuffer* buf, STMCanFrame* src) {
    // atomic access
    // buffer full
    if (((buf->read_idx_ - buf->write_idx_) & (buf->capacity_)) == 1) {
        return 0;
    }
    buf->data_[buf->write_idx_++] = *src;
    buf->write_idx_ &= buf->capacity_;
    return 1;
}

int RingBuffer_DequeueSTM(CanRingBuffer* buf, STMCanFrame* dst) {
    // buffer empty
    if (buf->read_idx_ == buf->write_idx_) {
        return 0;
    }

    *dst = buf->data_[buf->read_idx_++];
    buf->read_idx_ &= buf->capacity_;
    return 1;
}
