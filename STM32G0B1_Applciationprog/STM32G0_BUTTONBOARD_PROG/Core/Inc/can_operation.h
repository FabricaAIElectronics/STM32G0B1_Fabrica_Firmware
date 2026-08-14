/**
  ******************************************************************************
  * @file    can_operation.h
  * @brief   CAN layer for the fabricaAI calibration knob / button board V5.5.
  *
  * Bus parameters are the Fabrica house standard: classic CAN 2.0B, 500 kbit/s
  * nominal, 11-bit standard identifiers only. See `Docs/CAN_Bus.md`.
  *
  * ---------------------------------------------------------------------------
  * Address block
  * ---------------------------------------------------------------------------
  * The CiA-301 reserved gap (0x101-0x17F) that KincoDrive, PowerStage and
  * LEDDriver share is fully allocated — there is no room for a fourth device
  * there. This board therefore lives in 0x780-0x7BF, the range `Docs/CAN_Bus.md`
  * marks "Reserved (free)". It sits above CANopen's NMT error-control block
  * (0x700-0x77F) and below LSS (0x7E4/0x7E5), so it collides with nothing.
  *
  * The legacy knob IDs from the STM32F303 build (0x661-0x667 RX, 0x7E1 TX) are
  * NOT carried over: 0x667 falls inside a CANopen range that section 7 of
  * CAN_Bus.md retires, and 0x7E1 was shared by every bootloader on the bus.
  ******************************************************************************
  */

#ifndef INC_CAN_OPERATION_H_
#define INC_CAN_OPERATION_H_

#include "stm32g0xx_hal.h"
#include "main.h"
#include "eeprom_driver.h"

extern FDCAN_HandleTypeDef hfdcan1;

/* --------------------------------------------------------------------------
 * Identifiers
 * --------------------------------------------------------------------------
 * BTN_DEVICEID doubles as the OpenBLT bootloader RX id. One XCP CONNECT frame
 * (byte[0] = 0xFF, DLC = 2) on 0x780 resets a running application; the
 * bootloader then re-handles the identical frame on the identical id. This
 * MUST stay in step with BOOT_COM_CAN_RX_MSG_ID / BOOT_COM_CAN_TX_MSG_ID in
 * STM32G0B1_Bootloader/G0B1_ButtonBoard_Boot/App/blt_conf.h.
 */
#define BTN_DEVICEID            0x780U  /* RX: bootloader RX / app reset      */
#define BTN_BOOTLOADER_TX       0x781U  /* TX: OpenBLT XCP responses (FYI)    */

#define BTN_CMD_LED             0x790U  /* RX: LED set, DLC=2                 */
#define BTN_CMD_BUFFER          0x791U  /* RX: LED source select, DLC=1       */
#define BTN_CMD_ENCODER         0x792U  /* RX: preset encoder count, DLC=3    */
#define BTN_CMD_EEPROM          0x793U  /* RX: save / load defaults, DLC=1    */

#define BTN_BCAST_KNOBSTATE     0x7A0U  /* TX: encoder + rotary + state, 100ms*/
#define BTN_BCAST_BUTTONS       0x7A1U  /* TX: button matrix, 100 ms          */
#define BTN_BCAST_LEDSTATE      0x7A2U  /* TX: LED + buffer echo, 500 ms      */
#define BTN_BCAST_DEVSTATUS     0x7A3U  /* TX: state + error mask, 500 ms     */
#define BTN_BCAST_EEPROMDATA    0x7A4U  /* TX: config echo, 500 ms            */

/* Acceptance filter covers the whole sub-block so a future command id needs no
 * filter change. Named _RANGE_LOW/_RANGE_HIGH because the V&V conformance
 * stage recognises that suffix as a range marker rather than a message id. */
#define BTN_CAN_ID_RANGE_LOW      0x780U
#define BTN_CAN_ID_RANGE_HIGH     0x7BFU

/* --------------------------------------------------------------------------
 * Host -> board command state
 * --------------------------------------------------------------------------
 * Every field is written by the FDCAN RX ISR and read by the main loop, so
 * every field is volatile. Without it the compiler may cache a value in a
 * register across the main loop and never observe the interrupt's write —
 * the flags are the dangerous ones, because a hoisted load turns them into
 * flags that never appear to be set.
 */
typedef struct {
    volatile uint8_t  led_mask;         /* bits 0..5 -> LED_1..LED_6          */
    volatile uint8_t  led_int_mask;     /* bits 0..3 -> LED_INT_1..4          */
    volatile uint8_t  led_update;       /* set by ISR, cleared by main loop   */

    volatile uint8_t  buffer_source;    /* LED_SRC_DIRECT or LED_SRC_STM32    */
    volatile uint8_t  buffer_update;

    volatile int16_t  encoder_preset;   /* new count for CMD_ENCODER          */
    volatile uint8_t  encoder_preset_update;

    volatile uint8_t  eeprom_save;      /* CMD_EEPROM bit 0                   */
    volatile uint8_t  eeprom_load_defaults; /* CMD_EEPROM bit 1               */

    volatile uint8_t  flashdetected;    /* XCP traffic seen — hold off TX     */
} CAN_RXMessage;

extern CAN_RXMessage can_rxMessage;

void CAN_Init(void);
void CAN_Send(uint32_t id, uint8_t *data, uint8_t len);
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);

/* Suppress application traffic while a firmware download is in flight, so the
 * board's own broadcasts do not compete with XCP for bus bandwidth. Mirrors
 * the LEDDriver/PowerStage behaviour. */
void CAN_FlashDetectionTick(void);

/* Broadcast helpers — see Docs/CAN_Bus.md for the byte layouts. */
struct InputState;                      /* forward decl, see inputs.h         */
void CAN_BroadcastKnobState(const struct InputState *in, uint8_t state,
                            uint8_t buff_sel_nibble);
void CAN_BroadcastButtons(const struct InputState *in);
void CAN_BroadcastLedState(uint8_t led_mask, uint8_t led_int_mask,
                           uint8_t source);
void CAN_BroadcastDeviceStatus(uint8_t state, uint8_t errorMask);
void CAN_BroadcastEepromData(const Config *config);

#endif /* INC_CAN_OPERATION_H_ */
