#include "can_operation.h"
#include "main.h"
#include "eeprom_driver.h"
#include "header.h"
#include "stm32g0b1xx.h"
#include <string.h>

static FDCAN_TxHeaderTypeDef txMsgHeader = {
    .IdType = FDCAN_STANDARD_ID,
    .DataLength = FDCAN_DLC_BYTES_8,
    .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
    .BitRateSwitch = FDCAN_BRS_OFF,
    .FDFormat = FDCAN_CLASSIC_CAN,
    .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
    .MessageMarker = 0
};
static FDCAN_RxHeaderTypeDef rxMsgHeader;
static uint8_t txMsgData[8];
static uint8_t rxMsgData[8];

CAN_RXMessage can_rxMessage = {0};
eeprom_command eeprom_cmd = {0};

void CAN_Init(void){
    HAL_FDCAN_ActivateNotification(&canHandle, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    HAL_FDCAN_Start(&canHandle);

}
void CAN_Send(uint32_t id, uint8_t* data, uint8_t len){
    txMsgHeader.Identifier = id;
    txMsgHeader.DataLength = len;
    memcpy(txMsgData, data, len);
    uint32_t timeout = HAL_GetTick() + 1000;
    while (HAL_FDCAN_GetTxFifoFreeLevel(&canHandle) == 0) {
        if (HAL_GetTick() > timeout) {
            // Timeout occurred, handle the error (e.g., return or log an error message)
            return;
        }
    }
    HAL_FDCAN_AddMessageToTxFifoQ(&canHandle, &txMsgHeader, txMsgData);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs){
	memset(rxMsgData, 0, sizeof(rxMsgData));
    if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) {
        HAL_FDCAN_GetRxMessage(&canHandle, FDCAN_RX_FIFO0, &rxMsgHeader, rxMsgData);
        // Process the received message
        if (rxMsgHeader.Identifier == LIGHTSET) {
            can_rxMessage.pwm[0] = rxMsgData[0];
            can_rxMessage.pwm[1] = rxMsgData[1];
            can_rxMessage.pwm[2] = rxMsgData[2];

        }
        if (rxMsgHeader.Identifier == VOLTAGESET) {
            uint16_t voltage = (rxMsgData[0] << 8) | rxMsgData[1];
            can_rxMessage.under_voltage_24 = voltage;
            uint16_t voltage_1 = (rxMsgData[2] << 8) | rxMsgData[3];
            can_rxMessage.under_voltage_17_5 = voltage_1;
        }
        if ((rxMsgHeader.Identifier == DEVICEID)&&(rxMsgData[0] == 0xFF)) {
            // jump to bootloader
        	NVIC_SystemReset();
        }
        if(rxMsgHeader.Identifier == EEPROMSET){
            //check what is the command needed, bit 1 for write config to eeprom, bit 2 for reset default
            if(rxMsgData[0] & 0x01){ 
                //setflag to write config to eeprom in main loop
                eeprom_cmd.write_eeprom_flag = 1;
            }
        if(rxMsgData[0] & 0x02){ 
            //setflag to reset default in main loop
            eeprom_cmd.reset_default_flag = 1;
                }
            }
    }
}
void broadcastEEPROMData(Config *config){
    uint8_t data[7];
    data[0] = (config->under_voltage_24 >> 8) & 0xFF; // High byte of voltage
    data[1] = config->under_voltage_24 & 0xFF;        // Low byte of voltage
    data[2] = (config->under_voltage_17_5 >> 8) & 0xFF; // High byte of voltage
    data[3] = config->under_voltage_17_5 & 0xFF;        // Low byte of voltage
    data[4] = config->pwm0;
    data[5] = config->pwm1;
    data[6] = config->pwm2;
    
    CAN_Send(EEPROMDATA, data, 7);
}

void braodcastLEDStatus(LED_Peripheral_STATUS status){
    uint8_t data[8] = {0};
    static int count = 0;

    data[0] = status.pwm[0];
    data[1] = status.pwm[1];
    data[2] = status.pwm[2];
    data[3] = (status.voltage_24>>8) & 0xFF;
    data[4] = status.voltage_24 & 0xFF;
    data[5] = (status.voltage_17_5>>8) & 0xFF;
    data[6] = status.voltage_17_5 & 0xFF;
    data[7] = count;
    if(count>=255){
    	count = 0;
    }
    else{
    	count++;
    }

    CAN_Send(LIGHTSTATUS, data, 8);
}

void broadcastDeviceStatus(uint8_t state, uint8_t errorCode){
    uint8_t data[8];
    // Here you can fill the data array with the relevant device status information
    // For example, you might want to include error codes, temperature, or other diagnostics
    // For demonstration, we'll just set some dummy status values
    //system status, 0x01 init, 0x02 running, 0x03 error, etc.
    data[0] = state;
    data[1] = errorCode;


    CAN_Send(DEVSTATUS, data, 2);
}


