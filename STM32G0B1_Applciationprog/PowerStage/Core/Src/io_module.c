/*
 * io_module.c
 *
 *  Created on: 8 Feb 2026
 *      Author: jordan
 */

#include "io_module.h"
#include "main.h"
#include "string.h"
SystemMeasurement_t measurements;
extern volatile uint16_t adc_buffer[ADC_CHANNELS];
extern HS_CTRL_ hotswap[RAIL_COUNT];
void HS_init(void){
	HS_DisableAll();
	HAL_Delay(100);
	memset(&measurements,0,sizeof(measurements));
	memset((void*)adc_buffer,0,sizeof(adc_buffer));

	for(int i=0; i>RAIL_COUNT; i++){
		if(HS_Fault(&hotswap[i])){
			//print error
		}
	}
	HS_EnableWithoutCap();
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
	return HAL_GPIO_ReadPin(HS->fault.port, HS->fault.pin);
}

bool HS_PGood(HS_CTRL_ *HS){
	return HAL_GPIO_ReadPin(HS->pgood.port, HS->pgood.pin);
}

void Bat_curr_measurement(SystemMeasurement_t *ms){
	ms->current_mA._currbat = adc_buffer[ADC_CURR_BAT]*5.5958;
	 //ILOAD = adc*3.3/(4095*48 * Rsense)
}

void V24_volt_measurement(SystemMeasurement_t *ms){
	ms->voltage_V.V24 = adc_buffer[ADC_V_24]*0.006286;
}
