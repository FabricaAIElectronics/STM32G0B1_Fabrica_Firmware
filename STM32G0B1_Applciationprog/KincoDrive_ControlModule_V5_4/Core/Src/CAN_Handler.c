#include "CAN_Handler.h"
#include "main.h"
#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>


#include "fdcan.h"


/*Include all other file headers and place their CAN registration in CAN_handler_Init*/
#include "Fan_PWM.h"
#include "Power_Electronic.h"
#include "Endstop.h"
#include "eeprom_driver.h"
#include "error_manager.h"
#include "stm32g0xx_hal_fdcan.h"


#define CAN_SUCCESS 0
#define CAN_ERROR -1

//static FDCAN_HandleTypeDef hfdcan1; // FDCAN handle, initialized in MX_FDCAN1_Init()

typedef void (*can_handler_t)(uint32_t id, uint8_t *params, uint8_t len);

typedef struct { uint32_t id; uint8_t dlc; uint8_t data[8]; } can_frame_t;

/* ── Linear-search dispatch table (supports 29-bit extended CAN IDs) ── */
typedef struct {
    uint32_t      id;
    can_handler_t handler;
} can_dispatch_entry_t;

#define MAX_CAN_HANDLERS  16
static can_dispatch_entry_t dispatch_entries[MAX_CAN_HANDLERS];
static uint8_t dispatch_count = 0;

bool can_register_handler(uint32_t cmd, can_handler_t h)
{
    if (dispatch_count >= MAX_CAN_HANDLERS || h == NULL) return false;
    dispatch_entries[dispatch_count].id      = cmd & 0x1FFFFFFFU;
    dispatch_entries[dispatch_count].handler = h;
    dispatch_count++;
    return true;
}

static void handle_set_test_LED(uint32_t id, uint8_t *params, uint8_t len);

static uint8_t dlc_to_bytes(uint32_t dlc)
{
    switch (dlc) {
        case 0U:  return 0U;
        case 1U:  return 1U;
        case 2U:  return 2U;
        case 3U:  return 3U;
        case 4U:  return 4U;
        case 5U:  return 5U;
        case 6U:  return 6U;
        case 7U:  return 7U;
        case 8U:  return 8U;
        case 9U:  return 12U;
        case 10U: return 16U;
        case 11U: return 20U;
        case 12U: return 24U;
        case 13U: return 32U;
        case 14U: return 48U;
        case 15U: return 64U;
        default:  return 0U;
    }
}

/* ring buffer parameters */
#define RBUF_SIZE  16U
#define RBUF_MASK  (RBUF_SIZE - 1U)
static volatile uint32_t rbuf_head = 0;
static volatile uint32_t rbuf_tail = 0;
static can_frame_t rbuf[RBUF_SIZE];
static volatile uint32_t rbuf_drop_count = 0;

/* Producer: called from ISR context */
bool EnqueueCanFrame(const can_frame_t *frame)
{   
    
    uint32_t head = rbuf_head;
    uint32_t next = (head + 1U) & RBUF_MASK;

    if (next == rbuf_tail) {
        rbuf_drop_count++;
        return false; /* full */
    }

    /* copy frame into slot owned by producer */
    memcpy(&rbuf[head], frame, sizeof(can_frame_t));

    /* ensure data written before publishing head */
    __DMB();                    /* data memory barrier (ARM CMSIS) */
    rbuf_head = next;           /* publish (release) */

    /* optional: set a volatile flag or notify main loop */
    return true;
}

/* Consumer: called from main loop (non-ISR) */
bool DequeueCanFrame(can_frame_t *out)
{
    uint32_t tail = rbuf_tail;
    uint32_t head_snapshot = rbuf_head; /* snapshot of published head */

    if (tail == head_snapshot) {
        return false; /* empty */
    }

    /* copy out the frame */
    memcpy(out, &rbuf[tail], sizeof(can_frame_t));

    /* ensure copy done before updating tail */
    __DMB();
    rbuf_tail = (tail + 1U) & RBUF_MASK; /* publish new tail */

    return true;
}


void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan1, uint32_t RxFifo0ITs)
{
  FDCAN_RxHeaderTypeDef RxHeader;
  uint8_t RxData[8];

  if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0)
  {
    HAL_FDCAN_GetRxMessage(hfdcan1, FDCAN_RX_FIFO0, &RxHeader, RxData);

    if ((RxHeader.Identifier == 0x667)&&(RxData[0] == 0xFF)) {
        /*NECESSARY for handover to bootloader*/
        HAL_NVIC_SystemReset();
    }

    can_frame_t rx_frame;
    rx_frame.id = RxHeader.Identifier;
    rx_frame.dlc = dlc_to_bytes(RxHeader.DataLength);
    if (rx_frame.dlc > 8) rx_frame.dlc = 8;
    memcpy(rx_frame.data, RxData, rx_frame.dlc);
    EnqueueCanFrame(&rx_frame);
  }
}

static void VectorBase_Config(void)
{
  /* The constant array with vectors of the vector table is declared externally in the
   * c-startup code.
   */
  extern const unsigned long g_pfnVectors[];

  /* Remap the vector table to where the vector table is located for this program. */
  SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}

void Pre_CAN_Handler_Init(void)
{
    VectorBase_Config();
}

void CAN_Handler_Init(void)
{
    can_register_handler(CMD_SET_LED,handle_set_test_LED);
    can_register_handler(CMD_SET_FAN_PWM, CAN_Handle_set_Fan_PWM);
    can_register_handler(CMD_SET_HS_DRIVE_POWER, CAN_Handler_Set_HS_Drive_Power);
    can_register_handler(CMD_SET_HS_EXTRUDER_POWER, CAN_Handler_Set_HS_Extruder_Power);
    can_register_handler(CMD_SET_HS_SCRUBBING_POWER, CAN_Handler_Set_HS_Scrubbing_Power);
    can_register_handler(CMD_SET_EEPROM_CONFIG,  CAN_Handler_EEPROM_Write_Config);
    can_register_handler(CMD_READ_EEPROM_CONFIG, CAN_Handler_EEPROM_Read_Config);
    can_register_handler(CMD_DUMP_ERRORS, CAN_Handler_Dump_Errors);
    can_register_handler(CMD_RESET_ERROR, CAN_Handler_Reset_Error);

    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    
}

void CAN_Handler_Dispatch_Process_One(void)
{
    can_frame_t f;

    /* nothing to do */
    if (DequeueCanFrame(&f) == false) {
        //early return if there is nothing to process
        return;
    }
    /* simple routing: first data byte is the command id */
    if (f.dlc == 0U) {
        /* no command byte -> ignore or optionally send nack */
        return;
    }

    /* Exact match on full 29-bit extended CAN ID */
    uint32_t cmd = f.id & 0x1FFFFFFFU;

    /* Linear search through registered handlers */
    for (uint8_t i = 0; i < dispatch_count; ++i) {
        if (dispatch_entries[i].id == cmd) {
            dispatch_entries[i].handler(f.id, f.data, f.dlc);
            return;
        }
    }
    /* no handler found — optionally send_nack(f.id, cmd); */
}

int CAN_Handler_Broadcast(uint32_t period_ms)
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if ((now - last_tick) < period_ms) {
        return CAN_SUCCESS; /* not time yet */
    }
    last_tick = now;

    /* ── 0x600: Endstops, Voltages, Protection (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        /* Byte 0–1: endstop triggered + fault */
        (void)CAN_Packer_Endstop_and_ESTOP_2Byte(&payload[0], &payload[1], 2);

        /* Byte 2–3: 24V bus voltage (2 bytes LE, 0.1V) */
        (void)CAN_Packer_24V_Bus_2Byte(&payload[2], 2);

        /* Byte 4: 12V bus voltage (1 byte, 0.1V) */
        (void)CAN_Packer_12V_Bus_1Byte(&payload[4], 1);

        /* Byte 5: protection state (OC + OV packed) */
        (void)CAN_Packer_Protection_State_1Byte(&payload[5], 1);

        /* Byte 6–7: reserved (0) */

        FDCAN_SendFrame(0x600, payload, sizeof(payload));
    }

    /* ── 0x601: Currents (5 bytes) ── */
    {
        uint8_t payload[5] = {0};

        /* Byte 0–1: 24V bus current (2 bytes LE, 0.1A) */
        (void)CAN_Packer_24V_Bus_Current_1DP_2Byte(&payload[0], 2);

        /* Byte 2: drive module current (1 byte, 0.1A) */
        (void)CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_DRIVE, &payload[2], 1);

        /* Byte 3: extruder module current (1 byte, 0.1A) */
        (void)CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_EXTRUDER, &payload[3], 1);

        /* Byte 4: scrubbing module current (1 byte, 0.1A) */
        (void)CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_SCRUBBING, &payload[4], 1);

        FDCAN_SendFrame(0x601, payload, sizeof(payload));
    }

    /* ── 0x602: Temperatures (8 bytes) ── */
    {
        uint8_t payload[8] = {0};

        /* Byte 0–5: thermistor temperatures (offset-encoded: wire = °C + 40) */
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_1, &payload[0], 1);
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_2, &payload[1], 1);
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_3, &payload[2], 1);
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_4, &payload[3], 1);
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_5, &payload[4], 1);
        (void)CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_6, &payload[5], 1);

        /* Byte 6–7: reserved (0) */

        FDCAN_SendFrame(0x602, payload, sizeof(payload));
    }

    /* ── 0x603: Fan Speeds (5 bytes) ── */
    {
        uint8_t payload[5] = {0};

        /* Byte 0–4: fan speed (one byte per fan, 0–100%) */
        (void)CAN_Packer_Fan_Speed_1Byte(FAN_DR, &payload[0], 1);
        (void)CAN_Packer_Fan_Speed_1Byte(FAN_EP, &payload[1], 1);
        (void)CAN_Packer_Fan_Speed_1Byte(FAN_EH, &payload[2], 1);
        (void)CAN_Packer_Fan_Speed_1Byte(FAN_ST, &payload[3], 1);
        (void)CAN_Packer_Fan_Speed_1Byte(FAN_SF, &payload[4], 1);

        FDCAN_SendFrame(0x603, payload, sizeof(payload));
    }

    return CAN_SUCCESS;
}



static void handle_set_test_LED(uint32_t id, uint8_t *params, uint8_t len)
{
    if (len < 1U) {
        return; /* invalid length */
    }

    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);

    send_ack(0x700, 0x60);
}  
 

void send_ack(uint32_t dst_id, uint8_t cmd)
{
    uint8_t tx[2] = { cmd, 0x00U };
    (void)FDCAN_SendFrame(dst_id, tx, 2);
}
 
void send_nack(uint32_t dst_id, uint8_t cmd)
{
    uint8_t tx[2] = { cmd, 0xFFU };
    (void)FDCAN_SendFrame(dst_id, tx, 2);
}