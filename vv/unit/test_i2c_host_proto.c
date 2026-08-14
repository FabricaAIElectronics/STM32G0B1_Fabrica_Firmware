/* Host-side assertions for the V5.2-compatible I2C reply formats.
 * Compiles the real Core/Src/i2c_host_proto.c, battery.c-style. */
#include "harness.h"
#include "i2c_host_proto.h"

int main(void)
{
    uint8_t out[I2CHOST_MAX_REPLY];
    I2CHostProtoState st = {
        .knob_mv = 1650, .ref_mv = 3300, .encoder = -104, .button = 1,
        .uid = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04,0x05,0x06},
    };

    /* READ_ALL: 7 bytes, analog + encoder big-endian, button last. */
    VV_EQ_U32("read_all_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_ALL, &st, out), 7);
    VV_EQ_U32("read_all_knob_be", (out[0] << 8) | out[1], 1650);
    VV_EQ_U32("read_all_ref_be",  (out[2] << 8) | out[3], 3300);
    VV_EQ_U32("read_all_enc_be",  (int16_t)((out[4] << 8) | out[5]), (unsigned long)(int16_t)-104);
    VV_EQ_U32("read_all_button",  out[6], 1);

    /* Encoder read is BE; encoder write is LE - the ATtiny asymmetry. */
    VV_EQ_U32("enc_read_len", I2CHostProto_BuildReply(I2CHOST_CMD_RW_ENCODER, &st, out), 2);
    VV_EQ_U32("enc_read_be", (int16_t)((out[0] << 8) | out[1]), (unsigned long)(int16_t)-104);
    { uint8_t wr[2] = {0x9C, 0xFF};   /* -100 little-endian */
      VV_EQ_U32("enc_write_le", (unsigned long)(int16_t)I2CHostProto_ParseEncoderWrite(wr),
                (unsigned long)(int16_t)-100); }

    VV_EQ_U32("button_len", I2CHostProto_BuildReply(I2CHOST_CMD_RW_ENCODER_BUTTON, &st, out), 1);
    VV_EQ_U32("button_val", out[0], 1);

    VV_EQ_U32("version_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_VERSION, &st, out), 2);
    VV_EQ_U32("version_major", out[0], 2);
    VV_EQ_U32("version_minor", out[1], 0);

    VV_EQ_U32("uid_len", I2CHostProto_BuildReply(I2CHOST_CMD_READ_UID, &st, out), 10);
    VV_EQ_U32("uid_first", out[0], 0xDE);
    VV_EQ_U32("uid_last",  out[9], 0x06);

    /* Unknown command: full buffer of 0xFF so the slave never clock-starves. */
    VV_EQ_U32("unknown_len", I2CHostProto_BuildReply(0x42, &st, out), I2CHOST_MAX_REPLY);
    VV_EQ_U32("unknown_fill", out[0], 0xFF);

    /* Rotary ladder synthesis: pos * 550, hold-last on invalid (0xFF). */
    VV_EQ_U32("rotary_pos0", I2CHostProto_RotaryMv(0, 1234), 0);
    VV_EQ_U32("rotary_pos6", I2CHostProto_RotaryMv(6, 1234), 3300);
    VV_EQ_U32("rotary_hold", I2CHostProto_RotaryMv(0xFF, 1234), 1234);

    VV_REPORT();
}
