/*
 * can_operation.c
 *
 *  Created on: Oct 31, 2025
 *      Author: jordan
 */
#include "main.h"
#include "can_operation.h"
#include "8421_ENC.h"
#include "header.h"
#include <string.h>
extern CAN_HandleTypeDef canHandle;
CAN_TxHeaderTypeDef TxHeader;
uint32_t TxMailbox;


CAN_RxHeaderTypeDef RxHeader;
uint8_t CAN_Rxdata[8];
uint8_t gpio_status;
uint8_t Update_count =0;
static uint8_t gpio_command_update =0;


void CANInitTxHeader(){
//	TxHeader.StdId =0x010;
	TxHeader.IDE= CAN_ID_STD;
	TxHeader.RTR=CAN_RTR_DATA;
	TxHeader.DLC=8;
	TxHeader.TransmitGlobalTime=DISABLE;

	  HAL_CAN_ActivateNotification(&canHandle, CAN_IT_RX_FIFO0_MSG_PENDING);
}

HAL_StatusTypeDef CAN_Send(uint16_t canid, uint8_t dlc, uint8_t *data)
{
	uint32_t can_timeout = HAL_GetTick() +10;
	while(HAL_CAN_GetTxMailboxesFreeLevel(&canHandle)==0){
		if(HAL_GetTick()>can_timeout){
			return HAL_ERROR;
		}
	}
    TxHeader.StdId = canid;
    TxHeader.DLC = dlc;
	return HAL_CAN_AddTxMessage(&canHandle, &TxHeader, data, &TxMailbox);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan){

	/* Zero the buffer before every receive. HAL_CAN_GetRxMessage() only writes
	 * DLC bytes, and CAN_Rxdata is a file-scope global, so without this a short
	 * frame leaves stale bytes from the PREVIOUS frame visible below. */
	memset(CAN_Rxdata, 0, sizeof(CAN_Rxdata));

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, CAN_Rxdata) != HAL_OK) {
		return;
	}
	if(RxHeader.StdId==BOOTLOADER_RX_ID){
	if((CAN_Rxdata[0]==0xFF)&&(RxHeader.DLC==2)){
//		BootComCheckActivationRequest();
		NVIC_SystemReset();
	}
	}
	  /* KNOBCOMMAND carries the encoder-com bitmask in byte 7, so the frame must
	   * actually be 8 bytes long. A shorter frame is malformed - ignore it
	   * rather than acting on a zero/stale byte. */
	  if((RxHeader.StdId==KNOBCOMMAND) && (RxHeader.DLC >= 8U)){
		   gpio_status = CAN_Rxdata[7];
		   gpio_command_update = 1;
//		  SetEncoderCom(encoders[0], (gpio_status>>0) & 0x01);
//		  SetEncoderCom(encoders[1], ((gpio_status>>1) & 0x01));
//		  SetEncoderCom(encoders[2], ((gpio_status>>2) & 0x01));
	  }
}

//broadcast state of program in CAN

//broadcast position and button state in CAN
void CAN_Update_KnobState(uint8_t *comState, uint8_t *State){
	 uint8_t CAN_Txdata[8];
	 uint8_t val = *State;
	 uint8_t val1 = *comState;
	  CAN_Txdata[0] = encoders[0]->pos;
	  CAN_Txdata[1] = encoders[0]->but_state;
	  CAN_Txdata[2] = encoders[1]->pos;
	  CAN_Txdata[3] = encoders[1]->but_state;
	  CAN_Txdata[4] = encoders[2]->pos;
	  CAN_Txdata[5] = encoders[2]->but_state;
	  CAN_Txdata[6] = (val<<4)| (val1 & 0x0F) ;
	  CAN_Txdata[7] = Update_count;
	  if(Update_count>=255){
		  Update_count = 0;
	  }
	  else{
		  Update_count ++;
	  }
	  CAN_Send(KNOBSTATE, 8, CAN_Txdata);

}
//broadcast error

void CAN_Update_ErrorState(uint8_t *error){
	uint8_t CAN_Txdata;
	uint8_t error_val = *error;
	CAN_Txdata = error_val;
	CAN_Send(ERRORSTATE, 1, &CAN_Txdata);
}

uint8_t gpio_flag_check(void){
	return gpio_command_update;
}

void gpio_flag_reset(void){
	gpio_command_update=0;
}

void CAN_Update_ErrorCount(uint8_t *error_Count, uint8_t size){
	int array_size = size;
	uint8_t CAN_TxData[3];
	uint8_t Error_Count[3];
	memcpy(Error_Count,error_Count,array_size);
	for(int i=0; i<array_size; i++){
	CAN_TxData[i] = Error_Count[i];
	}
	CAN_Send(ERROR_COUNT_STATE, 3, CAN_TxData);

}
