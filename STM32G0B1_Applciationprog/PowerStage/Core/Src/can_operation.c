/*
 * can_operation.c
 *
 *  Created on: Oct 31, 2025
 *      Author: jordan
 */
#include "main.h"
#include "can_operation.h"
#include "header.h"
#include <string.h>
#include <stdbool.h>
extern FDCAN_HandleTypeDef canHandle;
FDCAN_TxHeaderTypeDef TxHeader;
CAN_STATUS canstat = {false,true};
uint8_t CAN_Rxdata[8];
uint8_t gpio_status;
uint8_t Update_count =0;



void CANInitTxHeader(){

	TxHeader.Identifier=0x100;
	TxHeader.IdType = FDCAN_STANDARD_ID;
	TxHeader.BitRateSwitch=FDCAN_BRS_OFF;
	TxHeader.DataLength=8;
	TxHeader.ErrorStateIndicator=FDCAN_ESI_PASSIVE;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;


	HAL_FDCAN_ActivateNotification(&canHandle, FDCAN_IT_RX_FIFO0_NEW_MESSAGE,0);
}

HAL_StatusTypeDef CAN_Send(uint16_t canid, uint8_t dlc, uint8_t *data)
{
	uint32_t can_timeout = HAL_GetTick() +10;
	while(HAL_FDCAN_GetTxFifoFreeLevel(&canHandle)==0){
		if(HAL_GetTick()>can_timeout){
			return HAL_ERROR;
		}
	}
    TxHeader.Identifier = canid;
    TxHeader.DataLength = dlc;
	return HAL_FDCAN_AddMessageToTxFifoQ(&canHandle, &TxHeader, data);
}

void HAL_CAN_RxFifo0MsgPendingCallback(FDCAN_HandleTypeDef *hcan){

	HAL_FDCAN_GetRxMessage(hcan, FDCAN_FLAG_RX_FIFO0_NEW_MESSAGE ,&RxHeader, CAN_Rxdata);
	if(RxHeader.Identifier==DEVICE_ADDR){
	if((CAN_Rxdata[0]==0xFF)&&(RxHeader.DataLength==2)){
//		BootComCheckActivationRequest();
		NVIC_SystemReset();
		if(canstat.system_update_detected==true){
			canstat.system_transmit_stat = false;
		}
	}
	}
	if(RxHeader.Identifier==KNOBCOMMAND){



	  }
}

//broadcast state of program in CAN

