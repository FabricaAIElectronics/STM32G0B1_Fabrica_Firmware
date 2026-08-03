/*
 * 8421_ENC.h
 *
 *  Created on: Oct 29, 2025
 *      Author: jordan
 */

#ifndef INC_8421_ENC_H_
#define INC_8421_ENC_H_

#include "main.h"

typedef struct {
	GPIO_TypeDef *outport;
	uint16_t outpin;
	GPIO_TypeDef *port[4];
	uint16_t pin[4];
	uint8_t pos;
	uint8_t pos_count;
	GPIO_TypeDef *but_port;
	uint16_t but_pin;
	uint8_t but_state;

}Encoder;
extern Encoder ENC1;
extern Encoder ENC2;
extern Encoder ENC3;
extern Encoder *encoders[3];
/* Number of times readEncoderPosStable() samples the lines before it will
 * believe a value, and the settle delay between those samples. Five samples
 * roughly 100 us apart span ~0.4 ms, enough to reject electrical noise on the
 * BCD lines. Mechanical bounce lasts longer than that, which is why the caller
 * also requires the verdict to persist across several 100 ms ticks. */
#define ENC_DEBOUNCE_SAMPLES   5U
#define ENC_DEBOUNCE_SETTLE    600U   /* ~100 us of NOPs at 72 MHz */

uint16_t readEncoderPos( Encoder *enc);

/* Sample the encoder repeatedly and only report a value the lines actually
 * agree on. `stable` is set to 1 when every sample matched, 0 when they did
 * not - a disagreement means the reading cannot be trusted, not that the
 * encoder is faulty. */
uint16_t readEncoderPosStable(Encoder *enc, uint8_t *stable);
uint16_t readEncoderbutton(Encoder *enc);
void SetEncoderCom(Encoder *enc, uint16_t State);
uint16_t readEncoderCom(Encoder *enc);

#endif /* INC_8421_ENC_H_ */
