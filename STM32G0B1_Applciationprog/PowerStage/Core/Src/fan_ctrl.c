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
		.htim              = &htim1,
		.channel           = TIM_CHANNEL_1,
		.Mode              = FAN_OFF,
		.min_dutycycle     = 10,
		.dutycycle_pct     = 0,
		.target_dutycycle  = 0,
		.auto_on_temp      = 50,    /* default: ON  above 50 °C */
		.auto_off_temp     = 45,    /* default: OFF below 45 °C */
		.ramp_step         = 5,     /* 5 % per tick             */
		.ramp_interval_ms  = 100,   /* tick every 100 ms → 2 s ramp */
		.ramp_tick         = 0
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
	/* Allow explicit 0 (fan-off path); clamp low-but-nonzero to min
	 * to prevent motor stall while spinning. */
	if (dutycycle > 0 && dutycycle < f->min_dutycycle) {
		f->dutycycle_pct = f->min_dutycycle;
	} else {
		f->dutycycle_pct = dutycycle;
	}
	uint32_t period  = __HAL_TIM_GET_AUTORELOAD(f->htim);
	uint32_t compare = (period * f->dutycycle_pct) / 100;
	__HAL_TIM_SET_COMPARE(f->htim, f->channel, compare);
}

/* ── FAN_AutoControl ─────────────────────────────────────────────────────── *
 * Hysteresis-based temperature control.                                       *
 * Only acts when fan->Mode == FAN_ON_AUTO.                                   *
 * Sets a target duty; FAN_RampUpdate() performs the gradual transition.      *
 * Turn ON  when temp ≥ auto_on_temp  °C (ramp to full-speed).                *
 * Turn OFF when temp <  auto_off_temp °C (ramp to zero, then disable GPIO).  *
 * ─────────────────────────────────────────────────────────────────────────── */
void FAN_AutoControl(FanCTRL_t *f, float temp_C)
{
    if (f->Mode != FAN_ON_AUTO) return;

    static bool s_running = false;

    if (!s_running && temp_C >= (float)f->auto_on_temp) {
        s_running = true;
        fan_ctrl_on();                /* enable hardware — ramp will bring speed up */
        f->target_dutycycle = 100;
    } else if (s_running && temp_C < (float)f->auto_off_temp) {
        s_running = false;
        f->target_dutycycle = 0;     /* ramp will bring speed down, then disable GPIO */
    }
}

/* ── FAN_RampUpdate ──────────────────────────────────────────────────────── *
 * Call every main-loop cycle.                                                 *
 * Steps dutycycle_pct toward target_dutycycle at ramp_step % per             *
 * ramp_interval_ms.  When the ramp-down reaches zero the fan GPIO is         *
 * disabled automatically.                                                     *
 * ─────────────────────────────────────────────────────────────────────────── */
void FAN_RampUpdate(FanCTRL_t *f)
{
    uint32_t now = HAL_GetTick();
    if ((now - f->ramp_tick) < (uint32_t)f->ramp_interval_ms) return;
    f->ramp_tick = now;

    uint8_t current = f->dutycycle_pct;
    uint8_t target  = f->target_dutycycle;

    if (current == target) return;

    if (current < target) {
        /* ── Ramp up ──────────────────────────────────────────── */
        uint16_t next16 = (uint16_t)current + f->ramp_step;
        uint8_t  next   = (next16 > target) ? target : (uint8_t)next16;
        fan_ctrl_speed(f, next);
    } else {
        /* ── Ramp down ────────────────────────────────────────── */
        uint8_t next = (current >= f->ramp_step) ? (current - f->ramp_step) : 0u;
        if (next < target) next = target;

        /* When target is 0 and next would fall into the stall-protection
         * zone, skip straight to zero so the ramp always reaches off. */
        if (target == 0u && next < f->min_dutycycle) next = 0u;

        if (next == 0u) {
            /* Final step — silence PWM then disable fan enable GPIO */
            f->dutycycle_pct = 0;
            __HAL_TIM_SET_COMPARE(f->htim, f->channel, 0);
            fan_ctrl_off();
        } else {
            fan_ctrl_speed(f, next);
        }
    }
}
