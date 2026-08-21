/* V5.2 ATtiny-compatible I2C host protocol - pure wire-format layer.
 * No HAL includes: this file also compiles on the host for vv/unit. */
#ifndef INC_I2C_HOST_PROTO_H_
#define INC_I2C_HOST_PROTO_H_

#include <stdint.h>

#define I2CHOST_ADDR_7BIT        0x51U  /* V5.2 strap wiring: 0x49 + 8      */

#define I2CHOST_CMD_READ_KNOB_1  0x05U
#define I2CHOST_CMD_READ_REF_V   0x08U
#define I2CHOST_CMD_READ_ALL     0x09U  /* power-on default, as on the ATtiny */
#define I2CHOST_CMD_READ_UID     0x10U
#define I2CHOST_CMD_RW_ENCODER   0x80U
#define I2CHOST_CMD_RW_ENCODER_BUTTON 0x84U
#define I2CHOST_CMD_READ_VERSION 0xFEU

#define I2CHOST_VERSION_MAJOR    2U     /* ATtiny reported 1,0 */
#define I2CHOST_VERSION_MINOR    0U

#define I2CHOST_UID_LEN          10U    /* ATtiny reply length; STM32 UID truncated */
#define I2CHOST_MAX_REPLY        16U
#define I2CHOST_REF_MV           3300U
#define I2CHOST_ROTARY_STEP_MV   550U   /* 6 x 10k ladder, COM at 3.3 V */

typedef struct {
    uint16_t knob_mv;                   /* synthesized rotary ladder voltage */
    uint16_t ref_mv;
    int16_t  encoder;
    uint8_t  button;
    uint8_t  uid[I2CHOST_UID_LEN];
} I2CHostProtoState;

/* Fill out[] with the reply for cmd; returns the reply length. Unknown
 * commands fill the whole buffer with 0xFF and return I2CHOST_MAX_REPLY. */
uint8_t I2CHostProto_BuildReply(uint8_t cmd, const I2CHostProtoState *st,
                                uint8_t out[I2CHOST_MAX_REPLY]);

/* rotary_pos 0..6 -> pos * 550 mV; anything else holds last_mv. */
uint16_t I2CHostProto_RotaryMv(uint8_t rotary_pos, uint16_t last_mv);

/* Encoder write payload is little-endian - the ATtiny asymmetry. */
int16_t I2CHostProto_ParseEncoderWrite(const uint8_t d[2]);

#endif /* INC_I2C_HOST_PROTO_H_ */
