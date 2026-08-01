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
			uint8_t delta[3] = {0,0,0};
			for(int i =0; i<3; i++){
			delta[i] = abs(pre_pos[i]-readEncoderPos(encoders[i]));
			if((delta[i])>8) delta[i]=1;
//			if((delta[i])<8) delta[i] +=16;
			if(delta[i]>=3){
				error_count[i]++;
				if(error_count[i]>5){
				error_state |= 1<<i;
				}
			}
			else{
//				error_state &= ~(1<<i);
//				if(error_count[i]!=0)
//				error_count[i]--;
//				pre_pos[i] = readEncoderPos(encoders[i]);
				encoders[i]->pos = readEncoderPos(encoders[i]);
				encoders[i]->but_state = readEncoderbutton(encoders[i]);

			}
			pre_pos[i] = readEncoderPos(encoders[i]);
//			encoders[i]->pos = readEncoderPos(encoders[i]);
//			encoders[i]->but_state = readEncoderbutton(encoders[i]);
			}
			//send error data
//			CAN_Update_ErrorCount(delta,3); for checking how much delta
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
