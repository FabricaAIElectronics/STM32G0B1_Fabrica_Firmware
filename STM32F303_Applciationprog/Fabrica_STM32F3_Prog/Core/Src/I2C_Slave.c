/*
 * I2C_Slave.c
 *
 *  Created on: Oct 27, 2025
 *      Author: jordan
 */

#include "main.h"
#include "I2C_Slave.h"
#include "8421_ENC.h"

#define KNOB1 0x05
#define KNOB2 0x06
#define KNOB3 0x07
extern I2C_HandleTypeDef hi2c1;

#define RXSIZE 1
uint8_t RXdata[RXSIZE];
uint8_t Txdata[6];
uint8_t Rev_I2C[1];

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c){
	HAL_I2C_EnableListen_IT(&hi2c1);
}

void processData(){
	switch(Rev_I2C[0]){
	case KNOB1: {
		Txdata[0] = ENC1.pos;
		Txdata[1] = ENC1.but_state;
	}
	break;
	case KNOB2:{
		Txdata[0] = ENC2.pos;
		Txdata[1] = ENC2.but_state;
//		Txdata[0] = 0x01;
//		Txdata[1] = 0x01;
	}
	break;
	case KNOB3:{
		Txdata[0] = ENC3.pos;
		Txdata[1] = ENC3.but_state;
//		Txdata[0] = 0x01;
//		Txdata[1] = 0x01;
	}
	break;
	default:{
		//Error_Handler();
	}
	break;
	}
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode){
	if(TransferDirection==I2C_DIRECTION_TRANSMIT){
		HAL_I2C_Slave_Seq_Receive_IT(&hi2c1, RXdata, 1, I2C_FIRST_AND_LAST_FRAME);
		Rev_I2C[0] = RXdata[0];
		processData();

//		HAL_I2C_Slave_Receive_IT(&hi2c1, RXdata, RXSIZE); cannot be used due to need to manage i2c ack and nack
	}
	else{
		HAL_I2C_Slave_Seq_Transmit_IT(&hi2c1, Txdata, 2, I2C_FIRST_AND_LAST_FRAME);
//		HAL_I2C_Slave_Transmit_IT(&hi2c1, RXdata, RXSIZE);
	}
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c){
	HAL_I2C_EnableListen_IT(&hi2c1);
}


