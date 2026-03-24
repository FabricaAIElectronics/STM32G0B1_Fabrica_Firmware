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
	uint8_t dutycycle_pct;
	uint8_t min_dutycycle;
	uint8_t auto_on_temp;   /* AUTO mode: turn ON  above this °C */
	uint8_t auto_off_temp;  /* AUTO mode: turn OFF below this °C (hysteresis) */
}FanCTRL_t;

/* Module-level fan instance defined in fan_ctrl.c — import with extern */
extern FanCTRL_t fan;

void fan_init(void);
void fan_ctrl_on(void);
void fan_ctrl_off(void);
void fan_ctrl_speed(FanCTRL_t *f, uint8_t dutycycle);
void FAN_AutoControl(FanCTRL_t *f, float temp_C);

#endif /* INC_FAN_CTRL_H_ */
