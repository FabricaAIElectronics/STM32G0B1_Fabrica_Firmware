/*
 * peripheral.c
 *
 *  Created on: 22 Feb 2026
 *      Author: jordan
 */

#include "stm32g0b1xx.h"
#include "main.h"
#include "peripheral.h"
extern TIM_HandleTypeDef htim1;
extern ADC_HandleTypeDef hadc1;

uint16_t ADC_VALUE[2];
AppContext app = {0};

void apply_pwm(AppContext *ctx)
{
    uint32_t channels[3] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3};
    uint32_t ARR_VALUE     = __HAL_TIM_GET_AUTORELOAD(&htim1);
    for (int i = 0; i < 3; i++) {
        uint32_t compare;
        if (ctx->pwm[i] == 0) {
            compare = 0;
        } else if (ctx->pwm[i] >= 100) {
            compare = ARR_VALUE + 1;
        } else {
            compare = ctx->pwm[i] * ARR_VALUE / 100;
        }
        __HAL_TIM_SET_COMPARE(&htim1, channels[i], compare);
    }
}

 void SET_BUCK(BUCK_STATUS buck_Status){
	switch (buck_Status){
	case DISABLE_BUCK:
		HAL_GPIO_WritePin(BUCK_EN_GPIO_Port, BUCK_EN_Pin, GPIO_PIN_RESET);
		break;
	case ENABLE_BUCK:
		HAL_GPIO_WritePin(BUCK_EN_GPIO_Port, BUCK_EN_Pin, GPIO_PIN_SET);
		break;
	default:
		break;
	}
}

 uint16_t READADC(ADC_CHANNEL channel){
	switch(channel){
	case V24_CHANNEL:{
		return (uint16_t)(ADC_VALUE[0] * 30360 / 4095);
		break;

	}
	case V17_5_CHANNEL:{
		return (uint16_t)(ADC_VALUE[1] * 30360 / 4095);
		break;
	}
	default:
		break;
	}
	return 0;
}

void processADC(ADC_CHANNEL channel,AppContext *app){
	switch(channel){
	case V24_CHANNEL:{
		app->V24_Value =(ADC_VALUE[0] * 30360 / 4095);
		break;

	}
	case V17_5_CHANNEL:{
		app->V17_5_Value = (ADC_VALUE[1] * 30360 / 4095);
		break;
	}
	default:
		break;
	}

}
// trigger dma adc measurement
void TrigerADCMEasurement(){
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)ADC_VALUE, 2);
}

// process dma triggered return call
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc){
	if(hadc->Instance== ADC1){
		processADC(V24_CHANNEL, &app);
		processADC(V17_5_CHANNEL, &app);



	}
}

