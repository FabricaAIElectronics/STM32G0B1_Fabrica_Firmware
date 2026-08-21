/* I2C1 host port: interrupt-driven slave at 0x51 serving the V5.2 ATtiny
 * protocol to an external master (Jetson) via connector P1. Pure slave -
 * the EEPROM lives on its own bus (I2C3). See i2c_host_proto.h for the
 * wire format and Docs/superpowers/specs/2026-08-14-i2c-host-compat-design.md. */
#ifndef INC_I2C_HOST_H_
#define INC_I2C_HOST_H_

#include "inputs.h"

/* Copy the STM32 UID into the reply state and arm slave listen mode.
 * Call once, after MX_I2C1_Init(). */
void I2CHost_Init(void);

/* Refresh the snapshot the slave ISR serves from. Called from the main
 * loop after Inputs_Poll; the ISR never touches InputState directly. */
void I2CHost_Publish(const InputState *in);

#endif /* INC_I2C_HOST_H_ */
