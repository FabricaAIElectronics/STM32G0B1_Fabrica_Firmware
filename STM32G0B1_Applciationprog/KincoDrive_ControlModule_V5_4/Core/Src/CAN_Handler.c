/**
 * @file    CAN_Handler.c
 * @brief   CAN RX mailbox + periodic broadcast.
 *
 * @details Pure transport:
 *            ISR      → stash last RX frame in single-slot mailbox
 *            AppLogic → poll with CAN_TryGetFrame() and dispatch itself
 *
 *          Broadcasts are split into 3 phases so the 3-deep TX FIFO never
 *          overflows even if the host isn't ACKing aggressively:
 *            Phase 0: Voltages + Currents
 *            Phase 1: Temps    + Fans
 *            Phase 2: GPIO
 *
 * @author  jordan
 * @date    2026-04-21
 */

#include "CAN_Handler.h"
#include "main.h"
#include "fdcan.h"

#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>

/* Module headers */
#include "AppLogic.h"
#include "Fan_PWM.h"
#include "Power_Electronic.h"
#include "Endstop.h"
#include "ESTOP.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  ISR → main-loop single-slot mailbox
 *
 *  If a second frame arrives before AppLogic consumes the first, the
 *  older frame is overwritten.  Fine for human-rate commands.
 * ═══════════════════════════════════════════════════════════════════════ */

static volatile bool          rx_pending = false;
static volatile CAN_RxFrame_t rx_slot;

/* Debug counters — exposed in Bcast_GPIO bytes 6/7. */
static volatile uint8_t dbg_isr_count = 0;   /* +1 per ISR RX        */
static          uint8_t dbg_cmd_count = 0;   /* +1 per dequeued frame */

/* ═══════════════════════════════════════════════════════════════════════
 *  FDCAN RX FIFO0 callback  (ISR context)
 * ═══════════════════════════════════════════════════════════════════════ */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if (!(RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE))
        return;

    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &hdr, data) != HAL_OK)
        return;

    /* Reject any extended frames — not our protocol. */
    if (hdr.IdType != FDCAN_STANDARD_ID)
        return;

    /* Bootloader entry: immediate reset (highest priority). */
    if ((hdr.Identifier == CAN_ID_BOOTLOADER) && (data[0] == 0xFFU)) {
        HAL_NVIC_SystemReset();
    }

    /* HAL DataLength is FDCAN_DLC_BYTES_x in bits [19:16]. */
    uint8_t dlc = (uint8_t)(hdr.DataLength >> 16);
    if (dlc > 8U) dlc = 8U;

    /* Populate mailbox (zero-pad unused bytes). */
    memset((void *)rx_slot.data, 0, 8);
    memcpy((void *)rx_slot.data, data, dlc);
    rx_slot.id  = (uint16_t)hdr.Identifier;
    rx_slot.dlc = dlc;

    dbg_isr_count++;
    __DMB();
    rx_pending = true;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Init
 * ═══════════════════════════════════════════════════════════════════════ */

static void vector_base_config(void)
{
    extern const unsigned long g_pfnVectors[];
    SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}

void Pre_CAN_Init(void)
{
    vector_base_config();
}

void CAN_Init(void)
{
    /* Global filter: accept all standard frames (we don't filter by ID;
     * AppLogic dispatches on the switch-case).  Reject extended + remote. */
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                     FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
        Error_Handler();

    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
        Error_Handler();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Consumer-side mailbox dequeue
 * ═══════════════════════════════════════════════════════════════════════ */

bool CAN_TryGetFrame(CAN_RxFrame_t *out)
{
    if (!rx_pending || out == NULL)
        return false;

    /* Copy volatile snapshot. */
    out->id  = rx_slot.id;
    out->dlc = rx_slot.dlc;
    memcpy(out->data, (const void *)rx_slot.data, 8);

    rx_pending = false;
    dbg_cmd_count++;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Broadcast frame builders
 * ═══════════════════════════════════════════════════════════════════════ */

static void send_voltages(void)
{
    uint8_t p[4] = {0};
    CAN_Packer_24V_Bus_mV_2Byte(&p[0], 2);
    CAN_Packer_12V_Bus_mV_2Byte(&p[2], 2);
    FDCAN_SendFrame(MSG_BCAST_VOLTAGES, p, 4);
}

static void send_currents(void)
{
    uint8_t p[8] = {0};
    CAN_Packer_24V_Bus_Current_mA_2Byte(&p[0], 2);
    CAN_Packer_HighSide_Module_Current_mA_2Byte(HS_MODULE_DRIVE,     &p[2], 2);
    CAN_Packer_HighSide_Module_Current_mA_2Byte(HS_MODULE_EXTRUDER,  &p[4], 2);
    CAN_Packer_HighSide_Module_Current_mA_2Byte(HS_MODULE_SCRUBBING, &p[6], 2);
    FDCAN_SendFrame(MSG_BCAST_CURRENTS, p, 8);
}

static void send_temps(void)
{
    uint8_t p[6] = {0};
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_1, &p[0], 1);
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_2, &p[1], 1);
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_3, &p[2], 1);
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_4, &p[3], 1);
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_5, &p[4], 1);
    CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_6, &p[5], 1);
    FDCAN_SendFrame(MSG_BCAST_TEMPS, p, 6);
}

static void send_fans(void)
{
    uint8_t p[5] = {0};
    CAN_Packer_Fan_Speed_1Byte(FAN_DR, &p[0], 1);
    CAN_Packer_Fan_Speed_1Byte(FAN_EP, &p[1], 1);
    CAN_Packer_Fan_Speed_1Byte(FAN_EH, &p[2], 1);
    CAN_Packer_Fan_Speed_1Byte(FAN_ST, &p[3], 1);
    CAN_Packer_Fan_Speed_1Byte(FAN_SF, &p[4], 1);
    FDCAN_SendFrame(MSG_BCAST_FANS, p, 5);
}

static void send_gpio(void)
{
    uint8_t p[8] = {0};

    /* Byte 0: HS enable + buck   (1 = enabled) */
    if (HAL_GPIO_ReadPin(HS_DR_EN_GPIO_Port,   HS_DR_EN_Pin))   p[0] |= 0x01U;
    if (HAL_GPIO_ReadPin(HS_E_EN_GPIO_Port,    HS_E_EN_Pin))    p[0] |= 0x02U;
    if (HAL_GPIO_ReadPin(HS_SC_EN_GPIO_Port,   HS_SC_EN_Pin))   p[0] |= 0x04U;
    if (HAL_GPIO_ReadPin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin)) p[0] |= 0x08U;

    /* Byte 1: PGOOD  (active-LOW → inverted: 1 = power good) */
    if (HAL_GPIO_ReadPin(HS_DR_PG_GPIO_Port, HS_DR_PG_Pin) == GPIO_PIN_RESET) p[1] |= 0x01U;
    if (HAL_GPIO_ReadPin(HS_E_PG_GPIO_Port,  HS_E_PG_Pin)  == GPIO_PIN_RESET) p[1] |= 0x02U;
    if (HAL_GPIO_ReadPin(HS_SC_PG_GPIO_Port, HS_SC_PG_Pin) == GPIO_PIN_RESET) p[1] |= 0x04U;

    /* Byte 2: FAULT  (active-LOW → inverted: 1 = fault asserted) */
    if (HAL_GPIO_ReadPin(HS_DR_FT_GPIO_Port, HS_DR_FT_Pin) == GPIO_PIN_RESET) p[2] |= 0x01U;
    if (HAL_GPIO_ReadPin(HS_E_FT_GPIO_Port,  HS_E_FT_Pin)  == GPIO_PIN_RESET) p[2] |= 0x02U;
    if (HAL_GPIO_ReadPin(HS_SC_FT_GPIO_Port, HS_SC_FT_Pin) == GPIO_PIN_RESET) p[2] |= 0x04U;

    /* Byte 3: ESTOP + misc
     *   bit 0 = NO pin raw        bit 1 = NC pin raw
     *   bit 2 = debounced state   bit 3 = wiring fault
     *   bit 4 = LED_OUT           bit 5 = BlueButton
     */
    if (HAL_GPIO_ReadPin(EStop_NO_INT_GPIO_Port, EStop_NO_INT_Pin))  p[3] |= 0x01U;
    if (HAL_GPIO_ReadPin(EStop_NC_INT_GPIO_Port, EStop_NC_INT_Pin))  p[3] |= 0x02U;
    if (ESTOP_State())                                                p[3] |= 0x04U;
    if (ESTOP_Check() == ESTOP_FAULT)                                 p[3] |= 0x08U;
    if (HAL_GPIO_ReadPin(LED_OUT_GPIO_Port,    LED_OUT_Pin))          p[3] |= 0x10U;
    if (HAL_GPIO_ReadPin(BlueButton_GPIO_Port, BlueButton_Pin))       p[3] |= 0x20U;

    /* Byte 4: Endstop NO raw pins  (1 = HIGH) */
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NO_INT_GPIO_Port, ENDSTOP_EH_H_NO_INT_Pin)) p[4] |= 0x01U;
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_L_NO_INT_GPIO_Port, ENDSTOP_EH_L_NO_INT_Pin)) p[4] |= 0x02U;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NO_INT_GPIO_Port, ENDSTOP_EP_H_NO_INT_Pin)) p[4] |= 0x04U;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NO_INT_GPIO_Port, ENDSTOP_EP_L_NO_INT_Pin)) p[4] |= 0x08U;
    if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NO_INT_GPIO_Port, ENDSTOP_SC_H_NO_INT_Pin)) p[4] |= 0x10U;

    /* Byte 5: Endstop NC raw pins  (1 = HIGH) */
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NC_INT_GPIO_Port, ENDSTOP_EH_H_NC_INT_Pin)) p[5] |= 0x01U;
    /* bit 1: EH_Bot is NO-only — reserved */
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NC_INT_GPIO_Port, ENDSTOP_EP_H_NC_INT_Pin)) p[5] |= 0x04U;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NC_INT_GPIO_Port, ENDSTOP_EP_L_NC_INT_Pin)) p[5] |= 0x08U;
    if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NC_INT_GPIO_Port, ENDSTOP_SC_H_NC_INT_Pin)) p[5] |= 0x10U;

    /* Byte 6: debug cmd-dispatch counter (wraps) */
    p[6] = dbg_cmd_count;

    /* Byte 7: debug ISR RX counter (wraps) */
    p[7] = dbg_isr_count;

    FDCAN_SendFrame(MSG_BCAST_GPIO, p, 8);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Periodic broadcast  (3 phases, staggered over period_ms)
 * ═══════════════════════════════════════════════════════════════════════ */

int CAN_Broadcast(uint32_t period_ms)
{
    static uint32_t last_tick = 0;
    static uint8_t  phase     = 0;

    uint32_t now      = HAL_GetTick();
    uint32_t interval = period_ms / 3U;

    if ((now - last_tick) < interval)
        return CAN_SUCCESS;
    last_tick = now;

    switch (phase) {
    case 0:
        send_voltages();
        send_currents();
        break;
    case 1:
        send_temps();
        send_fans();
        break;
    case 2:
        send_gpio();
        break;
    default:
        break;
    }

    phase++;
    if (phase > 2U) phase = 0U;

    return CAN_SUCCESS;
}
