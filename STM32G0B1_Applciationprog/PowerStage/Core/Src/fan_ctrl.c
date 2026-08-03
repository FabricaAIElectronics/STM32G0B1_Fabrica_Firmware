/*
 * fan_ctrl.c
 *
 *  Created on: 15 Mar 2026
 *      Author: jordan
 */
#include "fan_ctrl.h"
#include <stdbool.h>
extern TIM_HandleTypeDef htim1;
FanCTRL_t fan = {
		.htim          = &htim1,
		.channel       = TIM_CHANNEL_1,
		.Mode          = FAN_OFF,
		.min_dutycycle = 10,
		.dutycycle_pct = 10,
		.auto_on_temp  = 50,    /* default: ON  above 50 °C */
		.auto_off_temp = 45     /* default: OFF below 45 °C */
};

void fan_init(void){
	HAL_TIM_PWM_Start(fan.htim, fan.channel);
	__HAL_TIM_SET_COMPARE(fan.htim,fan.channel,0);


}

void fan_ctrl_on(){
	HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
}

void fan_ctrl_off(){
	HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_RESET);
}

void fan_ctrl_speed(FanCTRL_t *f, uint8_t dutycycle){
	/* Zero means STOP and is never clamped.
	 *
	 * min_dutycycle exists so a *running* fan is not commanded below the duty
	 * at which it stalls; it was never meant to prevent stopping. Clamping 0
	 * up to min_dutycycle made FAN_AutoControl's off branch - which calls
	 * fan_ctrl_speed(f, 0) - leave a 5 % compare value behind and report duty
	 * 5 on BCAST_FAN after the fan had been switched off. */
	if(dutycycle == 0U){
		f->dutycycle_pct = 0U;
	}
	else if(dutycycle < f->min_dutycycle){
		f->dutycycle_pct = f->min_dutycycle;
	}
	else{
		f->dutycycle_pct = dutycycle;
	}
	uint32_t period  = __HAL_TIM_GET_AUTORELOAD(f->htim);
	/* TIM1 output polarity is LOW (inverted), so invert the compare
	 * value so that a higher duty % = higher fan speed. */
//	uint32_t compare = period - (period * f->dutycycle_pct) / 100;
	uint32_t compare = (period * f->dutycycle_pct) / 100;

	__HAL_TIM_SET_COMPARE(f->htim, f->channel, compare);
}

/* ── FAN_AutoControl ─────────────────────────────────────────────────────── *
 * Hysteresis-based temperature control.                                       *
 * Only acts when fan->Mode == FAN_ON_AUTO.                                   *
 * Turn ON  when temp ≥ auto_on_temp  °C (full-speed).                        *
 * Turn OFF when temp <  auto_off_temp °C.                                    *
 * ─────────────────────────────────────────────────────────────────────────── */
void FAN_AutoControl(FanCTRL_t *f, float temp_C)
{
    if (f->Mode != FAN_ON_AUTO) return;

    static bool s_running = false;

    if (!s_running && temp_C >= (float)f->auto_on_temp) {
        s_running = true;
        fan_ctrl_on();
        fan_ctrl_speed(f, 100);
    } else if (s_running && temp_C < (float)f->auto_off_temp) {
        s_running = false;
        fan_ctrl_off();
        fan_ctrl_speed(f, 0);
    }
}
