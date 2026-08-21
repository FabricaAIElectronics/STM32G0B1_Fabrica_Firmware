#include "i2c_host_proto.h"

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

uint8_t I2CHostProto_BuildReply(uint8_t cmd, const I2CHostProtoState *st,
                                uint8_t out[I2CHOST_MAX_REPLY])
{
    switch (cmd) {
    case I2CHOST_CMD_READ_KNOB_1:
        put_be16(out, st->knob_mv);
        return 2U;
    case I2CHOST_CMD_READ_REF_V:
        put_be16(out, st->ref_mv);
        return 2U;
    case I2CHOST_CMD_READ_ALL:
        put_be16(&out[0], st->knob_mv);
        put_be16(&out[2], st->ref_mv);
        put_be16(&out[4], (uint16_t)st->encoder);
        out[6] = st->button;
        return 7U;
    case I2CHOST_CMD_RW_ENCODER:
        put_be16(out, (uint16_t)st->encoder);
        return 2U;
    case I2CHOST_CMD_RW_ENCODER_BUTTON:
        out[0] = st->button;
        return 1U;
    case I2CHOST_CMD_READ_VERSION:
        out[0] = I2CHOST_VERSION_MAJOR;
        out[1] = I2CHOST_VERSION_MINOR;
        return 2U;
    case I2CHOST_CMD_READ_UID:
        for (uint8_t i = 0U; i < I2CHOST_UID_LEN; i++) out[i] = st->uid[i];
        return I2CHOST_UID_LEN;
    default:
        for (uint8_t i = 0U; i < I2CHOST_MAX_REPLY; i++) out[i] = 0xFFU;
        return I2CHOST_MAX_REPLY;
    }
}

uint16_t I2CHostProto_RotaryMv(uint8_t rotary_pos, uint16_t last_mv)
{
    if (rotary_pos <= 6U) {
        return (uint16_t)(rotary_pos * I2CHOST_ROTARY_STEP_MV);
    }
    return last_mv;
}

int16_t I2CHostProto_ParseEncoderWrite(const uint8_t d[2])
{
    return (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
}
