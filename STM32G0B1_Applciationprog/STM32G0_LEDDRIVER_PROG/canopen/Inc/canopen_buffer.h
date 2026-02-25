#pragma once
#include "can_structs.h"
#include "canopen_frame.h"

typedef struct {
    STMCanFrame data_[8];
    uint32_t write_idx_;
    uint32_t read_idx_;
    uint32_t capacity_;
} CanRingBuffer;

/*
 * @brief convert between stm frame to canopen frame
 */
void CanOpenToStmTx(const CanOpen_Frame* const src, STMCanFrame* dst);
void StmToCanOpenRx(const STMCanFrame* const src, CanOpen_Frame* dst);

/*
 * @brief Create a new empty ring buffer
 */
CanRingBuffer RingBuffer_Create();

/*
 * @brief Empties the ring buffer
 */
void RingBuffer_Clear(CanRingBuffer* buf);

/*
 * enqueue and dequeue packets in both canopen and stmframe formats
 */
int RingBuffer_EnqueueCanOpen(CanRingBuffer* buf, CanOpen_Frame* src);
int RingBuffer_DequeueCanOpen(CanRingBuffer* buf, CanOpen_Frame* dst);
int RingBuffer_EnqueueSTM(CanRingBuffer* buf, STMCanFrame* src);
int RingBuffer_DequeueSTM(CanRingBuffer* buf, STMCanFrame* dst);

inline size_t RingBuffer_GetSize(CanRingBuffer* buf) { return (buf->write_idx_ - buf->read_idx_) & buf->capacity_; }
