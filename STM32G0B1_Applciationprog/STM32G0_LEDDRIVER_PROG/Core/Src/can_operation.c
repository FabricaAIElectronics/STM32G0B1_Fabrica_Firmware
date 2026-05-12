#include "can_operation.h"
#include "main.h"
#include "eeprom_driver.h"
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
static volatile uint32_t lastFlashMsgTick = 0;

void CAN_Init(void){
    /* RX filter: accept the LEDDriver sub-block 0x160..0x17F.
     * MX_FDCAN1_Init() must be configured with StdFiltersNbr >= 1 for this to apply. */
    FDCAN_FilterTypeDef filt = {0};
    filt.IdType       = FDCAN_STANDARD_ID;
    filt.FilterIndex  = 0;
    filt.FilterType   = FDCAN_FILTER_RANGE;
    filt.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filt.FilterID1    = 0x160;
    filt.FilterID2    = 0x17F;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filt);

    /* Reject everything not matching the filter (no extended frames either). */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT, FDCAN_REJECT,
                                 FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    HAL_FDCAN_Start(&hfdcan1);
}
/* HAL expects DataLength to be one of the bit-positioned FDCAN_DLC_BYTES_x
 * constants (e.g. FDCAN_DLC_BYTES_8 = 0x00080000) — NOT the raw byte count.
 * Passing a raw count makes the peripheral emit DLC=0 frames. */
static const uint32_t dlc_lut[9] = {
    FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
    FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
    FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8
};

void CAN_Send(uint32_t id, uint8_t* data, uint8_t len){
    if (len > 8U) len = 8U;

    /* Non-blocking: if FIFO is full, drop this frame. The phased broadcast
     * scheduler (one frame per 100 ms tick) ensures the FIFO has plenty of
     * time to drain between successive sends. */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0) return;

    txMsgHeader.Identifier = id;
    txMsgHeader.DataLength = dlc_lut[len];
    memcpy(txMsgData, data, len);
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txMsgHeader, txMsgData);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs){
	memset(rxMsgData, 0, sizeof(rxMsgData));
    if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) {
        HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rxMsgHeader, rxMsgData);
        // Process the received message
        if (rxMsgHeader.Identifier == LIGHTSET) {
        	for(int i =0; i <3; i++){
        		if(rxMsgData[i]>100)
        			can_rxMessage.pwm[i] = 100;
        		else if(rxMsgData[i]<0)
        			can_rxMessage.pwm[i] = 0;
        		else
        			can_rxMessage.pwm[i] = rxMsgData[i];
        	}
//            can_rxMessage.pwm[0] = rxMsgData[0];
//            can_rxMessage.pwm[1] = rxMsgData[1];
//            can_rxMessage.pwm[2] = rxMsgData[2];
            can_rxMessage.newcommandreceived = 1;

        }
        if (rxMsgHeader.Identifier == VOLTAGESET) {
            uint16_t voltage = (rxMsgData[0] << 8) | rxMsgData[1];
            can_rxMessage.under_voltage_24 = voltage;
            uint16_t voltage_1 = (rxMsgData[2] << 8) | rxMsgData[3];
            can_rxMessage.under_voltage_17_5 = voltage_1;

            /* DLC=5 form includes buck mode; older DLC=4 leaves mode untouched.
             * Clamp to valid range (0=OFF, 1=ON, 2=AUTO). */
            if (rxMsgHeader.DataLength == FDCAN_DLC_BYTES_5) {
                uint8_t mode = rxMsgData[4];
                if (mode > 0x02) mode = 0x02;
                can_rxMessage.buck_mode = mode;
            }

            can_rxMessage.newcommandreceived = 1;
        }
        if (rxMsgData[0] == 0xFF) {
            /* HAL fills DataLength with FDCAN_DLC_BYTES_x bit-positioned constants
             * (e.g. FDCAN_DLC_BYTES_2 = 0x00020000), NOT the raw byte count, so
             * the previous "==2" / "==1" checks never matched. */

            /* Bootloader entry: XCP CONNECT (byte0=0xFF, dlc=2) on DEVICEID. */
            if ((rxMsgHeader.Identifier == DEVICEID) &&
                (rxMsgHeader.DataLength == FDCAN_DLC_BYTES_2)) {
                NVIC_SystemReset();
            }

            /* Flash-in-progress detect (XCP poll, byte0=0xFF, dlc=1). */
            if (rxMsgHeader.DataLength == FDCAN_DLC_BYTES_1) {
                can_rxMessage.flashdetected = 1;
                lastFlashMsgTick = HAL_GetTick();
            }
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
    uint8_t data[8];
    data[0] = (config->under_voltage_24   >> 8) & 0xFF;   /* V24 UV high byte    */
    data[1] =  config->under_voltage_24         & 0xFF;   /* V24 UV low byte     */
    data[2] = (config->under_voltage_17_5 >> 8) & 0xFF;   /* V17.5 UV high byte  */
    data[3] =  config->under_voltage_17_5       & 0xFF;   /* V17.5 UV low byte   */
    data[4] = (uint8_t)config->pwm0;
    data[5] = (uint8_t)config->pwm1;
    data[6] = (uint8_t)config->pwm2;
    data[7] = config->buck_mode;                          /* 0=OFF 1=ON 2=AUTO   */

    CAN_Send(EEPROMDATA, data, 8);
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
// this function is to check is there flashing in progress
// if no 0xFF message received for 500ms, allow CAN transmit again
void FOCdetection(){
	if(HAL_GetTick() - lastFlashMsgTick > 500){
		can_rxMessage.flashdetected = 0;
	}
}

