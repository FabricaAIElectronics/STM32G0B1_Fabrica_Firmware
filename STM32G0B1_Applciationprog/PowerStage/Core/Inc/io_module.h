/*
 * io_module.h
 *
 *  Created on: 6 Feb 2026
 *      Author: jordan
 */

#ifndef INC_IO_MODULE_H_
#define INC_IO_MODULE_H_



#include "main.h"
#include "stdbool.h"

//typical Current monitor value

/* Combined constant: VRef x (R_SERIES + R_TERM) / (ADC_MAX x GAIN x R_TERM) x 1000
 * = 3.3 x 110000 / (4096 x 48 x 100000) x 1000
 * = (363000 / 19660800000) * 1000
 * = 7.440e-3  (updates automatically if any constant above changes) */
#define K_SCALE 7.440e-3f
#define RSENSE 0.003f
#define RSENSELED 0.020f
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} GPIO_Pin_t;

typedef enum {
	RAIL_AUX,
	RAIL_LED,
	RAIL_DRIVE,
	RAIL_CAP,
	RAIL_SBC,
	RAIL_COUNT
}PowerRailIndex_t;

typedef struct {
    GPIO_Pin_t enable;
    GPIO_Pin_t fault;
    GPIO_Pin_t pgood;
} HS_CTRL_;

#define ADC_CHANNELS 10
/* DMA is configured for WORD (32-bit) transfers in CubeMX —
 * buffer MUST be uint32_t to match DMA_MDATAALIGN_WORD.
 * ADC value sits in the lower 12 bits of each 32-bit word. */
extern volatile uint32_t adc_buffer[ADC_CHANNELS];

// ADC channel indices
typedef enum {
    ADC_CURR_BAT = 0,
    ADC_CURR_CAP,
    ADC_CURR_SBC,
    ADC_CURR_DRIVE,
    ADC_CURR_AUX,
    ADC_CURR_LED,
    ADC_V_24,
    ADC_V_CAP,
    ADC_V_12,
    ADC_V_NTC
} ADC_ChannelIndex_t;

typedef struct {
	struct {
		float _currbat;
		float _currcap;
		float _currsbc;
		float _currdrive;
		float _curraux;
		float _currled;
	}current_mA;

	struct {
		float V24;
		float VCAP;
		float V12;
	}voltage_V;

	float NTCTemperature_C;
}SystemMeasurement_t;

extern SystemMeasurement_t measurements;
extern HS_CTRL_ hotswap[RAIL_COUNT];


void HS_init(void);
void HS_Enable(HS_CTRL_ *HS);
void HS_EnableAll();
void HS_EnableWithoutCap();
void HS_Disable(HS_CTRL_ *HS);
void HS_DisableAll();
bool HS_Fault(HS_CTRL_ *HS);
bool HS_PGood(HS_CTRL_ *HS);

void Bat_curr_measurement(SystemMeasurement_t *ms);
void Cap_curr_measurement(SystemMeasurement_t *ms);
void Drive_curr_measurement(SystemMeasurement_t *ms);
void LED_curr_measurement(SystemMeasurement_t *ms);
void SBC_curr_measurement(SystemMeasurement_t *ms);
void AUX_curr_measurement(SystemMeasurement_t *ms);
void NTC_Temp_measurement(SystemMeasurement_t *ms);

void V24_volt_measurement(SystemMeasurement_t *ms);
void VCAP_volt_measurement(SystemMeasurement_t *ms);
void V12_volt_measurement(SystemMeasurement_t *ms);
#endif /* INC_IO_MODULE_H_ */
