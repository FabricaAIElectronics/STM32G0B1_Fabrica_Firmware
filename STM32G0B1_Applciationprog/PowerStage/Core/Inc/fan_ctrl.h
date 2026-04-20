/*
 * fan_ctrl.h
 *
 *  Created on: 15 Mar 2026
 *      Author: jordan
 */

#ifndef INC_FAN_CTRL_H_
#define INC_FAN_CTRL_H_

#include "main.h"
#include "stdbool.h"

typedef enum {
	FAN_OFF,
	FAN_ON,
	FAN_ON_AUTO
}fanMode_t;

typedef struct {
	TIM_HandleTypeDef *htim;
	uint32_t channel;
	fanMode_t Mode;
	uint8_t dutycycle_pct;        /* current hardware duty cycle (0-100%) */
	uint8_t target_dutycycle;     /* ramp destination (0-100%)            */
	uint8_t min_dutycycle;
	uint8_t auto_on_temp;         /* AUTO mode: turn ON  above this °C */
	uint8_t auto_off_temp;        /* AUTO mode: turn OFF below this °C (hysteresis) */
	uint8_t  ramp_step;           /* duty-% step per ramp tick            */
	uint16_t ramp_interval_ms;    /* ms between ramp steps                */
	uint32_t ramp_tick;           /* HAL_GetTick of last ramp step        */
}FanCTRL_t;

/* Module-level fan instance defined in fan_ctrl.c — import with extern */
extern FanCTRL_t fan;

void fan_init(void);
void fan_ctrl_on();
void fan_ctrl_off();
void fan_ctrl_speed(FanCTRL_t *f, uint8_t dutycycle);
void FAN_AutoControl(FanCTRL_t *f, float temp_C);
void FAN_RampUpdate(FanCTRL_t *f);

#endif /* INC_FAN_CTRL_H_ */
