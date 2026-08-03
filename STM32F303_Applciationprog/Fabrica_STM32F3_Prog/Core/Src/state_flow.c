/*
 * state_flow.c
 *
 *  Created on: Nov 3, 2025
 *      Author: jordan
 */


#include "state_flow.h"
#include "main.h"
#include "I2C_Slave.h"
#include "8421_ENC.h"
#include "can_operation.h"
#include <stdlib.h>

uint8_t CAN_Txdata[8];

// private variable like statemachine, register and others

#define INIT 0x00
#define PRECHECK 0x01
#define RUNNING 0x02
#define ERROR 0x03

uint8_t State = 0x00;

#define INTERVAL_GPIO 500
#define UPDATE_INTERVAL 100
#define UPDATE_INTERVAL1 500
#define PRECHECK_DURATION 1000
#define INIT_DURATION 500
#define ERROR_CHECK_DURATION 100
#define ERROR_RESET_DURATION 2000

/* Sustained-evidence window for the encoder fault check.
 *
 * One sample every ERROR_CHECK_DURATION ms, and a fault is only declared when
 * at least ERROR_WINDOW_MIN_BAD of the last ERROR_WINDOW_SAMPLES samples were
 * bad. At 100 ms that is 4 bad readings inside a 500 ms window.
 *
 * The previous logic incremented a counter that was never decremented - the
 * decrement was commented out - so six transient glitches at ANY point in the
 * board's life latched an error bit permanently. Combined with an undebounced
 * GPIO read, the counters reported faults that were really just noise while a
 * knob was being turned. The window is a shift register, so it self-clears:
 * stop seeing bad samples and the count falls back to zero on its own. */
#define ERROR_WINDOW_SAMPLES 5U
#define ERROR_WINDOW_MIN_BAD 4U
#define ERROR_DELTA_FAULT    3U   /* counts of jump that look like a fault  */
#define ERROR_DELTA_WRAP     8U   /* above this it is 15->0 wraparound      */
unsigned long timer_count = 0;
unsigned long last_timer_init = 0;
unsigned long last_timer_precheck =0;
unsigned long last_timer_running_gpio_update =0;
unsigned long last_timer_running_state_update =0;
unsigned long last_timer_running_state_update1=0;
unsigned long last_timer_running_error_check =0;
uint8_t pre_pos[3];
uint8_t error_flag=0;
uint8_t error_state = 0;
uint8_t error_count[3] = {0,0,0};
/* One bit per sample, newest in bit 0. Only the low ERROR_WINDOW_SAMPLES bits
 * are examined, so this is a rolling 500 ms view of each encoder. */
static uint8_t error_hist[3] = {0,0,0};

/* How many of the last ERROR_WINDOW_SAMPLES samples were bad. */
static uint8_t error_window_bad(uint8_t hist)
{
	uint8_t bad = 0U;
	for (uint8_t b = 0U; b < ERROR_WINDOW_SAMPLES; b++) {
		if (hist & (1U << b)) {
			bad++;
		}
	}
	return bad;
}
// only start running when everything is initialized
void Operation_run(){
	switch(State){
	case INIT:{
		timer_count = HAL_GetTick();
		if((timer_count-last_timer_init)>INIT_DURATION){
			State = PRECHECK;
			CAN_Update_KnobState(NULL, &State);
			last_timer_precheck = timer_count;
		}
	}
	break;
	case PRECHECK:{
		//set comm gpio low, make sure all com postion output is zero, if pass, turn on com and move to running, if fail, go to error
		timer_count = HAL_GetTick();
		for (int i = 0; i <3; i++){
		SetEncoderCom(encoders[i], 0);
		}

		if((timer_count-last_timer_precheck)>PRECHECK_DURATION){
			uint8_t gpio_check=0;
			for(int i =0; i <3; i++){
				uint16_t pos=readEncoderPos(encoders[i]);
				if(pos>0){
					gpio_check = 1;
					last_timer_init = HAL_GetTick();
					break;
				}
			}
			CAN_Update_KnobState(NULL, &State);
			//set back all encoder to be activate
			if(gpio_check==1){
				State = ERROR;
			}
			else{
			for (int i = 0; i <3; i++){
			SetEncoderCom(encoders[i], 1);
			pre_pos[i]=readEncoderPos(encoders[i]);
			}
			State = RUNNING;
			error_state = 0;
			last_timer_running_gpio_update = HAL_GetTick();
			last_timer_running_state_update = HAL_GetTick();
			last_timer_running_state_update1 = HAL_GetTick();
			last_timer_running_error_check = HAL_GetTick();
		}
		}

	}
	break;
	case RUNNING: {
		timer_count = HAL_GetTick();
		//read from message, set gpio output based on can received
		if((timer_count-last_timer_running_gpio_update)>=INTERVAL_GPIO){
			if(gpio_flag_check()==1){
				  SetEncoderCom(encoders[0], (gpio_status>>0) & 0x01);
				  SetEncoderCom(encoders[1], ((gpio_status>>1) & 0x01));
				  SetEncoderCom(encoders[2], ((gpio_status>>2) & 0x01));
				  gpio_flag_reset();
			}
				  last_timer_running_gpio_update = HAL_GetTick();

		}
		//routine broadcast CAN information on encoder position, button state and alive counter
		if((timer_count-last_timer_running_state_update)>=UPDATE_INTERVAL){
			uint8_t com_state=0;
			for (int i =0; i <3 ; i++){
//			encoders[i]->pos = readEncoderPos(encoders[i]);
//			pre_pos[i] = encoders[i]->pos;
//			encoders[i]->but_state = readEncoderbutton(encoders[i]);
			com_state |= (readEncoderCom(encoders[i])& 0x01)<<i;
			}
			CAN_Update_KnobState(&com_state, &State);
			CAN_Update_ErrorCount(error_count,3);
//			CAN_Update_Firmware_Ver();
//			CAN_Update_ErrorState(&error_state);


			last_timer_running_state_update = HAL_GetTick();
		}
		if((timer_count-last_timer_running_state_update1)>=UPDATE_INTERVAL1){


					CAN_Update_ErrorState(&error_state);
//					CAN_Update_Firmware_Ver();


					last_timer_running_state_update1 = HAL_GetTick();
				}
		if((timer_count-last_timer_running_error_check)>=ERROR_CHECK_DURATION){

			/* Skip encoder drift checking while a host GPIO command is still
			 * pending. Was `~gpio_flag_check()`: gpio_flag_check() returns 0 or
			 * 1, and bitwise-complement promotes to int, so ~0 == -1 and
			 * ~1 == -2 -- both non-zero. The guard was ALWAYS true, so drift
			 * checking ran during com-line changes, which is exactly when
			 * spurious position deltas appear, producing false error_state
			 * bits. Logical negation is what was meant. */
			if(!gpio_flag_check()){
			for(int i =0; i<3; i++){
			uint8_t stable = 0U;
			uint8_t pos = (uint8_t)readEncoderPosStable(encoders[i], &stable);
			uint8_t bad;

			if (!stable) {
				/* The lines disagreed with themselves inside one read. That is
				 * an untrustworthy sample, so record it as bad and move on -
				 * one bad sample cannot declare a fault by itself. Crucially
				 * pre_pos is NOT updated from a value we do not believe. */
				bad = 1U;
			} else {
				uint8_t delta = (uint8_t)abs((int)pre_pos[i] - (int)pos);
				if (delta > ERROR_DELTA_WRAP) {
					/* 4-bit encoder: a large delta is the 15->0 wrap, not a jump. */
					delta = (uint8_t)(16U - delta);
				}
				bad = (delta >= ERROR_DELTA_FAULT) ? 1U : 0U;
				if (!bad) {
					encoders[i]->pos = pos;
					encoders[i]->but_state = readEncoderbutton(encoders[i]);
				}
				pre_pos[i] = pos;
			}

			/* Shift the verdict into the rolling window and judge the window,
			 * never the single sample. */
			error_hist[i] = (uint8_t)((error_hist[i] << 1) | bad);
			error_count[i] = error_window_bad(error_hist[i]);

			if (error_count[i] >= ERROR_WINDOW_MIN_BAD) {
				error_state |= (uint8_t)(1U << i);
			} else if (error_count[i] == 0U) {
				/* A full clean window clears the fault: a knob that was noisy
				 * while being handled must not stay flagged forever. */
				error_state &= (uint8_t)~(1U << i);
			}
			}
			}

			last_timer_running_error_check = HAL_GetTick();
		}



		//set routine to check for encoder increment and decrement. (if skip count raise warning or error)
	}
	break;
	case ERROR: {
		//anything error stays here until system reset
		timer_count= HAL_GetTick();
		if((timer_count-last_timer_init)>=ERROR_RESET_DURATION){
			State= INIT;
			last_timer_init = HAL_GetTick();
			CAN_Update_KnobState(NULL, &State);
		}


	}
	break;
	default:
	{
		//do nothing
	}
	break;
	}
}
