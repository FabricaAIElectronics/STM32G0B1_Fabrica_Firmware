#include "i2c_host.h"
#include "i2c_host_proto.h"
#include "can_operation.h"   /* encoder preset flags                     */
#include "main.h"            /* hi2c1                                    */

/* Snapshot written by the main loop, read by the slave ISR. Same volatile
 * discipline as CAN_RXMessage: without it a hoisted load serves stale
 * bytes forever. */
static volatile I2CHostProtoState s_state;
static volatile uint8_t  s_last_cmd = I2CHOST_CMD_READ_ALL; /* ATtiny default */
static uint16_t s_last_mv = 0U;

static uint8_t s_rx[4];                  /* cmd + optional 2-byte payload, +1
                                           * guard: I2C_ITSlaveCplt/ITListenCplt
                                           * write one more byte past pBuffPtr
                                           * with no bounds check of their own. */
static uint8_t s_tx[I2CHOST_MAX_REPLY];
static uint8_t s_tx_pad[I2CHOST_MAX_REPLY]; /* all-0xFF; re-armed chunk for a
                                           * master that keeps clocking past
                                           * the documented reply length. */

/* Set when AddrCallback arms the 2-byte encoder payload receive; cleared once
 * that payload is consumed (or a write-direction address restarts a command).
 * HAL_I2C_SlaveRxCpltCallback branches on this instead of on hi2c->pBuffPtr,
 * which is fragile once s_rx is padded past the payload it points into. */
static volatile uint8_t s_expect_payload;

void I2CHost_Init(void)
{
    const uint32_t w[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) {
        s_state.uid[i] = (uint8_t)(w[i / 4U] >> (8U * (i % 4U)));
    }
    s_state.ref_mv = I2CHOST_REF_MV;
    for (uint8_t i = 0U; i < I2CHOST_MAX_REPLY; i++) s_tx_pad[i] = 0xFFU;
    HAL_NVIC_SetPriority(I2C1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_IRQn);
    (void)HAL_I2C_EnableListen_IT(&hi2c1);
}

void I2CHost_Publish(const InputState *in)
{
    s_last_mv        = I2CHostProto_RotaryMv(in->rotary_pos, s_last_mv);
    s_state.knob_mv  = s_last_mv;
    s_state.encoder  = in->encoder_pos;
    s_state.button   = in->encoder_button;
}

/* ---- HAL slave callbacks ------------------------------------------------ */

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t dir, uint16_t addr)
{
    (void)addr;
    if (hi2c->Instance != I2C1) return;

    if (dir == I2C_DIRECTION_TRANSMIT) {
        /* Master writes: first byte is the command. */
        s_expect_payload = 0U;
        if (HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx[0], 1U, I2C_NEXT_FRAME) != HAL_OK) {
            /* Failed to arm: NACK the transaction instead of stretching
             * SCL forever waiting for a receive that will never start. */
            __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_ADDR);
        }
    } else {
        /* Master reads: serve the reply for the last command. Copy the
         * volatile snapshot to a plain struct for the pure builder. */
        I2CHostProtoState st;
        st.knob_mv = s_state.knob_mv;  st.ref_mv  = s_state.ref_mv;
        st.encoder = s_state.encoder;  st.button  = s_state.button;
        for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) st.uid[i] = s_state.uid[i];

        uint8_t len = I2CHostProto_BuildReply(s_last_cmd, &st, s_tx);
        /* Transmit exactly the documented reply length with NEXT_FRAME: a
         * master that reads exactly len bytes then NACKs+STOPs lands on the
         * XferCount==0 branch of I2C_Slave_ISR_IT's AF handling, which goes
         * straight through I2C_ITSlaveSeqCplt() to HAL_I2C_SlaveTxCpltCallback
         * below with no error code set. If we instead armed the full 16-byte
         * buffer with I2C_LAST_FRAME (as before), XferCount would still be
         * nonzero when STOP lands, and NEXT_FRAME/LAST_FRAME both force the
         * transfer through the HAL_I2C_ERROR_AF path in I2C_ITSlaveCplt for
         * every reply shorter than 16 bytes - i.e. every real reply. Worse,
         * LAST_FRAME leaves TXI enabled once XferCount hits 0 (the ISR's
         * TXIS branch only calls I2C_ITSlaveSeqCplt for FIRST_FRAME/
         * NEXT_FRAME), so it either spins on dummy TXIS events or, if the
         * master keeps reading past byte 16, clock-stretches forever
         * (NoStretch is disabled) - a hard lockup. HAL_I2C_SlaveTxCpltCallback
         * re-arms a 0xFF pad chunk with NEXT_FRAME so an over-reading master
         * keeps getting 0xFF bytes instead. */
        if (HAL_I2C_Slave_Seq_Transmit_IT(hi2c, s_tx, len, I2C_NEXT_FRAME) != HAL_OK) {
            __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_ADDR);
        }
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    /* The reply just drained (either the real len-byte reply or a previous
     * pad chunk) and the master is still clocking SCL. Re-arm another 16
     * bytes of 0xFF: if the master stops here instead, this chunk simply
     * never gets consumed and the transaction ends via ListenCplt. */
    (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, s_tx_pad, I2CHOST_MAX_REPLY,
                                        I2C_NEXT_FRAME);
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    if (!s_expect_payload) {
        /* Command byte just landed. */
        s_last_cmd = s_rx[0];
        if (s_last_cmd == I2CHOST_CMD_RW_ENCODER) {
            /* An encoder write may follow: cmd + 2 LE bytes. */
            s_expect_payload = 1U;
            (void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx[1], 2U,
                                               I2C_NEXT_FRAME);
        }
        /* Other commands carry no payload; STOP lands in ListenCplt. */
    } else {
        /* Payload complete: route through the same preset path CMD_ENCODER
         * uses on CAN, so there is one apply site in applogic.c. */
        s_expect_payload = 0U;
        can_rxMessage.encoder_preset =
            I2CHostProto_ParseEncoderWrite(&s_rx[1]);
        can_rxMessage.encoder_preset_update = 1U;
    }
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    (void)HAL_I2C_EnableListen_IT(hi2c);   /* re-arm for the next START */
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) return;
    /* AF here is I2C_ITSlaveCplt's generic "STOP landed before XferCount
     * reached 0" signal, not necessarily a real bus fault - it fires for
     * example when the master stops mid pad-chunk after a well-formed read.
     * I2C_ITError() takes two different paths depending on the state it
     * captured on entry:
     *   - From LISTEN/BUSY_TX_LISTEN/BUSY_RX_LISTEN, it keeps LISTEN active
     *     and hands off to I2C_ITListenCplt(), which is what actually calls
     *     HAL_I2C_ListenCpltCallback() (our re-arm of EnableListen_IT) for
     *     that path. Re-arming here too would double up on an already-live
     *     listen.
     *   - Otherwise it drops to HAL_I2C_STATE_READY with no further
     *     recovery queued, so this is the only callback that will re-arm
     *     listen for that case.
     * Gate on State == READY so this only covers the second, non-LISTEN
     * path. */
    if (hi2c->State == HAL_I2C_STATE_READY) {
        (void)HAL_I2C_EnableListen_IT(hi2c);
    }
}
