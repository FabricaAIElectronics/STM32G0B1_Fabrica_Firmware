/*
 * peripheral.h
 *
 *  Created on: 23 Feb 2026
 *      Author: jorda
 */

#ifndef INC_PERIPHERAL_H_
#define INC_PERIPHERAL_H_

extern uint16_t ADC_VALUE[2];

typedef struct {
	uint8_t pwm[3];
	uint16_t V24_Value; //24VDC in mV
	uint16_t V17_5_Value; //17.5VDC in mV

}AppContext;


typedef enum {
	V24_CHANNEL = 0x00,
	V17_5_CHANNEL
}ADC_CHANNEL;

typedef enum {
	DISABLE_BUCK = 0x00,
	ENABLE_BUCK  = 0x01
}BUCK_STATUS;

void SET_BUCK(BUCK_STATUS buck_Status);
void TrigerADCMEasurement();
uint16_t READADC(ADC_CHANNEL channel);
void apply_pwm(AppContext *ctx);
void processADC(ADC_CHANNEL channel,AppContext *app);
#endif /* INC_PERIPHERAL_H_ */
