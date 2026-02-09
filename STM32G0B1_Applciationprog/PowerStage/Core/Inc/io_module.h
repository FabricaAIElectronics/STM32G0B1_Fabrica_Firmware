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
volatile uint16_t adc_buffer[ADC_CHANNELS];

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

HS_CTRL_ hotswap[RAIL_COUNT] = {
	[RAIL_AUX] = {
			.enable = {EN_AUX_GPIO_Port,EN_AUX_Pin},
			.fault = {FLT_AUX_GPIO_Port,FLT_AUX_Pin},
			.pgood = {PGOOD_AUX_GPIO_Port,PGOOD_AUX_Pin}
	},

	[RAIL_LED] = {
			.enable = {EN_LED_GPIO_Port,EN_LED_Pin},
			.fault = {FLT_LED_GPIO_Port,FLT_LED_Pin},
			.pgood = {PGOOD_LED_GPIO_Port,PGOOD_LED_Pin}
	},

	[RAIL_DRIVE] = {
			.enable = {EN_DRIVE_GPIO_Port,EN_DRIVE_Pin},
			.fault = {FLT_DRIVE_GPIO_Port,FLT_DRIVE_Pin},
			.pgood = {PGOOD_DRIVE_GPIO_Port,PGOOD_DRIVE_Pin}
	},

	[RAIL_CAP] = {
			.enable = {EN_CAP_GPIO_Port,EN_CAP_Pin},
			.fault = {FLT_CAP_GPIO_Port,FLT_CAP_Pin},
			.pgood = {PGOOD_CAP_GPIO_Port,PGOOD_CAP_Pin}
	},

	[RAIL_SBC] = {
			.enable = {NULL},
			.fault = {FLT_SBC_GPIO_Port,FLT_SBC_Pin},
			.pgood = {PGOOD_SBC_GPIO_Port,PGOOD_SBC_Pin}
	}
};

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

void V24_volt_measurement(SystemMeasurement_t *ms);
#endif /* INC_IO_MODULE_H_ */
