/*
 * 8421_ENC.c
 *
 *  Created on: Oct 29, 2025
 *      Author: jordan
 */

#include "main.h"
#include "8421_ENC.h"
//typedef struct {
//	GPIO_TypeDef *outport[1];
//	uint16_t outpin[1];
//	GPIO_TypeDef *port[4];
//	uint16_t pin[4];
//	uint8_t pos;
//	uint8_t pos_count;
//	GPIO_TypeDef but_port;
//	uint16_t but_pin;
//	uint8_t but_state;
//
//}Encoder;

Encoder ENC1 = {
		.port = { ENC_8_1_GPIO_Port, ENC_4_1_GPIO_Port, ENC_2_1_GPIO_Port, ENC_1_1_GPIO_Port},
		.pin = { ENC_8_1_Pin, ENC_4_1_Pin ,ENC_2_1_Pin, ENC_1_1_Pin},
		.outport = ENC_COM_1_GPIO_Port,
		.outpin = ENC_COM_1_Pin,
		.but_port = ENC_BUT_1_GPIO_Port,
		.but_pin = ENC_BUT_1_Pin,
		.but_state = 1,
		.pos =0,
		.pos_count =4
		};

Encoder ENC2 = {
		.port = { ENC_8_2_GPIO_Port, ENC_4_2_GPIO_Port, ENC_2_2_GPIO_Port, ENC_1_2_GPIO_Port},
		.pin = { ENC_8_2_Pin, ENC_4_2_Pin ,ENC_2_2_Pin, ENC_1_2_Pin},
		.outport = ENC_COM_2_GPIO_Port,
		.outpin = ENC_COM_2_Pin,
		.but_port = ENC_BUT_2_GPIO_Port,
		.but_pin = ENC_BUT_2_Pin,
		.but_state = 1,
		.pos =0,
		.pos_count =4
		};

Encoder ENC3 = {
		.port = { ENC_8_3_GPIO_Port, ENC_4_3_GPIO_Port, ENC_2_3_GPIO_Port, ENC_1_3_GPIO_Port},
		.pin = { ENC_8_3_Pin, ENC_4_3_Pin ,ENC_2_3_Pin, ENC_1_3_Pin},
		.outport = ENC_COM_3_GPIO_Port,
		.outpin = ENC_COM_3_Pin,
		.but_port = ENC_BUT_3_GPIO_Port,
		.but_pin = ENC_BUT_3_Pin,
		.but_state = 1,
		.pos =0,
		.pos_count =4
		};
Encoder *encoders[] = {&ENC1, &ENC2, &ENC3};
uint16_t readEncoderPos( Encoder *enc){
	uint8_t value =0;
	for(int i =0; i<4; i++){
		uint16_t bit= (HAL_GPIO_ReadPin(enc->port[i], enc->pin[i]));
		value |= bit<<((enc->pos_count)-1-i);
	}
	//enc->pos = value;
	return value;
}

/* Debounced read. A single pass over the four BCD lines catches whatever
 * happens to be on them at that instant, so a line still settling reads as a
 * position several counts away from the truth. That is what produced spurious
 * entries in the error counters: the reading was wrong, not the encoder.
 *
 * Sample ENC_DEBOUNCE_SAMPLES times and only report the value if every sample
 * agreed. `stable` tells the caller whether to trust the result at all. */
uint16_t readEncoderPosStable(Encoder *enc, uint8_t *stable)
{
	uint16_t first = readEncoderPos(enc);
	uint8_t  agree = 1U;

	for (uint8_t s = 1U; s < ENC_DEBOUNCE_SAMPLES; s++) {
		for (volatile uint32_t d = 0U; d < ENC_DEBOUNCE_SETTLE; d++) {
			__NOP();
		}
		if (readEncoderPos(enc) != first) {
			agree = 0U;
			/* Keep sampling rather than returning early: the loop is short and
			 * a constant execution time keeps the 100 ms tick predictable. */
		}
	}

	if (stable != NULL) {
		*stable = agree;
	}
	return first;
}

uint16_t readEncoderbutton(Encoder *enc){
	uint8_t state =0;
	state = HAL_GPIO_ReadPin(enc->but_port, enc->but_pin);
	//enc->but_state = state;
	return state;
}

void SetEncoderCom(Encoder *enc, uint16_t State){
	HAL_GPIO_WritePin(enc->outport, enc->outpin, State);
}

uint16_t readEncoderCom(Encoder *enc){
	int state =HAL_GPIO_ReadPin(enc->outport, enc->outpin);
	return state;
}


