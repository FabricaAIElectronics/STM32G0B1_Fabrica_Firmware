/*
 * io_module.c
 *
 *  Created on: 8 Feb 2026
 *      Author: jordan
 */

#include "io_module.h"
#include "main.h"
#include "string.h"
#include "math.h"
SystemMeasurement_t measurements;
volatile uint32_t adc_buffer[ADC_CHANNELS];


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


void HS_init(void){
	HS_DisableAll();
	HAL_Delay(100);
	memset(&measurements,0,sizeof(measurements));
	memset((void*)adc_buffer,0,sizeof(adc_buffer));

	for(int i=0; i<RAIL_COUNT; i++){
		if(HS_Fault(&hotswap[i])){
			//print error
		}
	}
//	HS_EnableWithoutCap();
}

void HS_Enable(HS_CTRL_ *HS){
	if((HS->enable.port==NULL)||(HS->enable.pin==0))
		return;
	HAL_GPIO_WritePin(HS->enable.port, HS->enable.pin, GPIO_PIN_SET);
}

void HS_EnableAll(){
	HS_Enable(&hotswap[RAIL_AUX]);
	HS_Enable(&hotswap[RAIL_CAP]);
	HS_Enable(&hotswap[RAIL_DRIVE]);
	HS_Enable(&hotswap[RAIL_LED]);
}

void HS_EnableWithoutCap(){
	HS_Enable(&hotswap[RAIL_AUX]);
	HS_Enable(&hotswap[RAIL_DRIVE]);
	HS_Enable(&hotswap[RAIL_LED]);
}

void HS_Disable(HS_CTRL_ *HS){
	if((HS->enable.port==NULL)||(HS->enable.pin==0))
			return;
	HAL_GPIO_WritePin(HS->enable.port, HS->enable.pin, GPIO_PIN_RESET);
}

void HS_DisableAll(){
	HS_Disable(&hotswap[RAIL_AUX]);
	HS_Disable(&hotswap[RAIL_CAP]);
	HS_Disable(&hotswap[RAIL_DRIVE]);
	HS_Disable(&hotswap[RAIL_LED]);
}

bool HS_Fault(HS_CTRL_ *HS){
	return HAL_GPIO_ReadPin(HS->fault.port, HS->fault.pin)==GPIO_PIN_RESET;
}

bool HS_PGood(HS_CTRL_ *HS){
	/* TPS2493 PGOOD is active-low open-drain: LOW = power good */
	return HAL_GPIO_ReadPin(HS->pgood.port, HS->pgood.pin)==GPIO_PIN_RESET;
}

void Bat_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currbat = (uint16_t)((adc_buffer[ADC_CURR_BAT]*K_SCALE)/RSENSE);
}

void Cap_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currcap = (uint16_t)((adc_buffer[ADC_CURR_CAP]*K_SCALE)/RSENSE);
}

void SBC_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currsbc = (uint16_t)((adc_buffer[ADC_CURR_SBC]*K_SCALE)/RSENSE);
}

void Drive_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currdrive = (uint16_t)((adc_buffer[ADC_CURR_DRIVE]*K_SCALE)/RSENSE);
}

void LED_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currled = (uint16_t)((adc_buffer[ADC_CURR_LED]*K_SCALE)/RSENSELED);
}

void AUX_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._curraux = (uint16_t)((adc_buffer[ADC_CURR_AUX]*K_SCALE)/RSENSE);
}
void NTC_Temp_measurement(SystemMeasurement_t *ms){
	float r_ntc = 10000.0f *((float)adc_buffer[ADC_V_NTC]/(4096 - (float)adc_buffer[ADC_V_NTC]));
	// recalculate resistor value based on Rntc = R1 * ADC/ADCmax-ADC
	float temp_k = 1.0f/((1.0f/298.15f)+(1.0f/3380.0f)*logf(r_ntc/10000));
	// temp_k = 1/((1/t0_k) + (1/B)*log(r_temp/r_temp0))
	ms->NTCTemperature_C = temp_k - 273.15f;
}

void V24_volt_measurement(SystemMeasurement_t *ms){
	ms->voltage_mV.V24 = (uint16_t)(adc_buffer[ADC_V_24] * K_V24_MV_PER_COUNT);
}

void VCAP_volt_measurement(SystemMeasurement_t *ms){
	ms->voltage_mV.VCAP = (uint16_t)(adc_buffer[ADC_V_CAP] * K_VCAP_MV_PER_COUNT);
}

void V12_volt_measurement(SystemMeasurement_t *ms){
	ms->voltage_mV.V12 = (uint16_t)(adc_buffer[ADC_V_12] * K_V12_MV_PER_COUNT);
}
