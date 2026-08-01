/*
 * can_operation.h
 *
 *  Created on: Oct 31, 2025
 *      Author: jordan
 */

#ifndef INC_CAN_OPERATION_H_
#define INC_CAN_OPERATION_H_
#include "main.h"
#include "stm32f3xx.h"

#define KNOBSTATE 0x661
#define ERRORSTATE 0x662
#define FIRMWARE_VER 0x663
#define ERROR_COUNT_STATE 0x664
#define KNOBCOMMAND 0x665

/* Bootloader RX / XCP CONNECT id. Must match BOOT_COM_CAN_RX_MSG_ID in the
 * matching bootloader's App/blt_conf.h (Fabrica_STM32F3RE_Boot). */
#define BOOTLOADER_RX_ID 0x667

extern CAN_HandleTypeDef canHandle;
extern CAN_TxHeaderTypeDef TxHeader;
extern CAN_RxHeaderTypeDef RxHeader;
extern uint32_t TxMailbox;
extern uint8_t gpio_status;
extern uint8_t CAN_Rxdata[8];
void CANInitTxHeader(void);
HAL_StatusTypeDef CAN_Send(uint16_t canid, uint8_t dlc, uint8_t *data);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan);
void CAN_Update_KnobState(uint8_t *comState, uint8_t *State);
void CAN_Update_ErrorState(uint8_t *error);
uint8_t gpio_flag_check();
void gpio_flag_reset(void);
void CAN_Update_Firmware_Ver(void);
void CAN_Update_ErrorCount(uint8_t *error_Count, uint8_t size);
#endif /* INC_CAN_OPERATION_H_ */
