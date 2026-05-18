/*
 * peripheral.h
 *
 *  Created on: 23 Feb 2026
 *      Author: jorda
 */

#ifndef INC_PERIPHERAL_H_
#define INC_PERIPHERAL_H_

/* DMA1 Channel 1 writes ADC1 conversion results here asynchronously.
 * Width MUST match the CubeMX "Memory Data Width" setting (Half Word →
 * uint16_t). 'volatile' tells the compiler not to cache reads, since
 * the value can change between any two source-level accesses. */
extern volatile uint16_t ADC_VALUE[2];

typedef struct {
	volatile uint8_t pwm[3];
	volatile uint16_t V24_Value; //24VDC in mV
	volatile uint16_t V17_5_Value; //17.5VDC in mV

}AppContext;


typedef enum {
	V24_CHANNEL = 0x00,
	V17_5_CHANNEL
}ADC_CHANNEL;

typedef enum {
	DISABLE_BUCK = 0x00,
	ENABLE_BUCK  = 0x01
}BUCK_STATUS;

/* Buck control mode set via CAN VOLTAGESET (byte 4).
 *
 *   BUCK_MANUAL_OFF — force buck OFF regardless of voltage.
 *   BUCK_MANUAL_ON  — force buck ON regardless of voltage.
 *                     Useful for bench testing; will keep buck on even in
 *                     STATE_ERROR (i.e. defeats the UV-triggered shutdown).
 *   BUCK_AUTO       — buck is enabled when V24 >= under_voltage_24 threshold,
 *                     disabled otherwise. The under_voltage_24 value sent
 *                     via VOLTAGESET acts as both the UV trip threshold AND
 *                     the buck-enable threshold.
 */
typedef enum {
	BUCK_MANUAL_OFF = 0x00,
	BUCK_MANUAL_ON  = 0x01,
	BUCK_AUTO       = 0x02
}BUCK_MODE_t;

void SET_BUCK(BUCK_STATUS buck_Status);
void TrigerADCMEasurement();
uint16_t READADC(ADC_CHANNEL channel);
void apply_pwm(AppContext *ctx);
void processADC(ADC_CHANNEL channel,AppContext *app);
#endif /* INC_PERIPHERAL_H_ */
