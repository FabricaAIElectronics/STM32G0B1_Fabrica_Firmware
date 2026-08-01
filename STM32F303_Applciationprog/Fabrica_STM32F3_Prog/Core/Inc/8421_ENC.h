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
uint16_t readEncoderPos( Encoder *enc);
uint16_t readEncoderbutton(Encoder *enc);
void SetEncoderCom(Encoder *enc, uint16_t State);
uint16_t readEncoderCom(Encoder *enc);

#endif /* INC_8421_ENC_H_ */
