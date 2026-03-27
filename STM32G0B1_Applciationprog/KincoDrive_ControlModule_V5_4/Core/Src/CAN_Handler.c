/**
 * @file    CAN_Handler.c
 * @brief   CAN bus command handling and telemetry broadcast.
 *
 * @details Architecture (kept intentionally simple for prototype/test):
 *
 *          ISR  →  saves last received frame into a global variable
 *          Main →  CAN_Process() reads the global, runs a switch-case
 *                  CAN_Broadcast() sends 6 telemetry frames every 500 ms
 *
 *          To add a new command:
 *            1. Define MSG_CMD_xxx in CAN_Handler.h
 *            2. Add a case in CAN_Process()
 *            — that's it.
 *
 * @author  jordan
 * @date    2026-03-25
 */

#include "CAN_Handler.h"
#include "main.h"
#include "fdcan.h"
#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>

/* Module headers */
#include "Fan_PWM.h"
#include "Power_Electronic.h"
#include "Endstop.h"
#include "ESTOP.h"
#include "eeprom_driver.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  ISR → main-loop message passing  (single-slot, no queue)
 *
 *  If a second CAN frame arrives before the main loop processes the
 *  first one, the older frame is overwritten.  For prototype testing
 *  at human command rates this is perfectly fine.
 * ═══════════════════════════════════════════════════════════════════════ */

static volatile bool rx_pending = false;
static volatile uint32_t rx_id;
static volatile uint8_t  rx_data[8];
static volatile uint8_t  rx_len;

/* ── Debug counters (visible in broadcast) ────────────────────────────── */
static volatile uint8_t  dbg_isr_count  = 0;   /* increments each ISR RX */
static          uint8_t  dbg_cmd_count  = 0;   /* increments each dispatched cmd */
static          uint16_t dbg_last_msg   = 0;   /* last msg_type dispatched */

/* ── Forward declaration ──────────────────────────────────────────────── */
static void Send_Config_Broadcast(void);

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

    /* Bootloader entry: immediate reset (highest priority) */
    if ((hdr.Identifier == CAN_ID_BOOTLOADER) && (data[0] == 0xFF)) {
        HAL_NVIC_SystemReset();
    }

    /* Save frame for main-loop processing */
    uint8_t dlc = (uint8_t)(hdr.DataLength);  /* HAL already extracts DLC code (0–15) */
    if (dlc > 8U) dlc = 8U;
    memset((void *)rx_data, 0, 8);          /* zero whole buffer before copy */
    memcpy((void *)rx_data, data, dlc);
    rx_len = dlc;
    rx_id  = hdr.Identifier;
    dbg_isr_count++;                                  /* debug: count ISR hits */
    __DMB();
    rx_pending = true;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Helper: build and send the GPIO status frame (periodic broadcast)
 * ═══════════════════════════════════════════════════════════════════════ */

static void Send_GPIO_Status_Frame(void)
{
    uint8_t p[8] = {0};

    /* --- Byte 0: High-side ENABLE pins (1 = enabled) ---
     *   bit 0 = HS_DR_EN  (Drive)
     *   bit 1 = HS_E_EN   (Extruder)
     *   bit 2 = HS_SC_EN  (Scrubbing)
     *   bit 3 = VBUCK_CTRL (12 V buck)
     */
    if (HAL_GPIO_ReadPin(HS_DR_EN_GPIO_Port,   HS_DR_EN_Pin))   p[0] |= 0x01;
    if (HAL_GPIO_ReadPin(HS_E_EN_GPIO_Port,    HS_E_EN_Pin))    p[0] |= 0x02;
    if (HAL_GPIO_ReadPin(HS_SC_EN_GPIO_Port,   HS_SC_EN_Pin))   p[0] |= 0x04;
    if (HAL_GPIO_ReadPin(VBUCK_CTRL_GPIO_Port, VBUCK_CTRL_Pin)) p[0] |= 0x08;

    /* --- Byte 1: High-side POWER-GOOD pins  (active LOW → inverted: 1 = good) ---
     *   bit 0 = HS_DR_PG
     *   bit 1 = HS_E_PG
     *   bit 2 = HS_SC_PG
     */
    if (HAL_GPIO_ReadPin(HS_DR_PG_GPIO_Port, HS_DR_PG_Pin) == GPIO_PIN_RESET) p[1] |= 0x01;
    if (HAL_GPIO_ReadPin(HS_E_PG_GPIO_Port,  HS_E_PG_Pin)  == GPIO_PIN_RESET) p[1] |= 0x02;
    if (HAL_GPIO_ReadPin(HS_SC_PG_GPIO_Port, HS_SC_PG_Pin) == GPIO_PIN_RESET) p[1] |= 0x04;

    /* --- Byte 2: High-side FAULT pins  (active LOW → inverted: 1 = fault) ---
     *   bit 0 = HS_DR_FT
     *   bit 1 = HS_E_FT
     *   bit 2 = HS_SC_FT
     */
    if (HAL_GPIO_ReadPin(HS_DR_FT_GPIO_Port, HS_DR_FT_Pin) == GPIO_PIN_RESET) p[2] |= 0x01;
    if (HAL_GPIO_ReadPin(HS_E_FT_GPIO_Port,  HS_E_FT_Pin)  == GPIO_PIN_RESET) p[2] |= 0x02;
    if (HAL_GPIO_ReadPin(HS_SC_FT_GPIO_Port, HS_SC_FT_Pin) == GPIO_PIN_RESET) p[2] |= 0x04;

    /* --- Byte 3: ESTOP ---
     *   bit 0 = NO raw pin  (1 = pin HIGH)
     *   bit 1 = NC raw pin  (1 = pin HIGH)
     *   bit 2 = debounced state (1 = triggered)
     *   bit 3 = wiring fault    (1 = fault)
     */
    if (HAL_GPIO_ReadPin(EStop_NO_INT_GPIO_Port, EStop_NO_INT_Pin)) p[3] |= 0x01;
    if (HAL_GPIO_ReadPin(EStop_NC_INT_GPIO_Port, EStop_NC_INT_Pin)) p[3] |= 0x02;
    if (ESTOP_State())                                               p[3] |= 0x04;
    if (ESTOP_Check() == ESTOP_FAULT)                                p[3] |= 0x08;

    /* --- Byte 4: Endstop NO pins raw  (1 = pin HIGH) ---
     *   bit 0 = EH_Top  NO
     *   bit 1 = EH_Bot  NO  (NO-only switch)
     *   bit 2 = EP_Top  NO
     *   bit 3 = EP_Bot  NO
     *   bit 4 = SC_Top  NO
     */
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NO_INT_GPIO_Port, ENDSTOP_EH_H_NO_INT_Pin)) p[4] |= 0x01;
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_L_NO_INT_GPIO_Port, ENDSTOP_EH_L_NO_INT_Pin)) p[4] |= 0x02;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NO_INT_GPIO_Port, ENDSTOP_EP_H_NO_INT_Pin)) p[4] |= 0x04;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NO_INT_GPIO_Port, ENDSTOP_EP_L_NO_INT_Pin)) p[4] |= 0x08;
    if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NO_INT_GPIO_Port, ENDSTOP_SC_H_NO_INT_Pin)) p[4] |= 0x10;

    /* --- Byte 5: Endstop NC pins raw  (1 = pin HIGH) ---
     *   bit 0 = EH_Top  NC
     *   bit 1 = reserved (EH_Bot has no NC)
     *   bit 2 = EP_Top  NC
     *   bit 3 = EP_Bot  NC
     *   bit 4 = SC_Top  NC
     */
    if (HAL_GPIO_ReadPin(ENDSTOP_EH_H_NC_INT_GPIO_Port, ENDSTOP_EH_H_NC_INT_Pin)) p[5] |= 0x01;
    /* bit 1: n/a (EH_Bot is NO-only) */
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_H_NC_INT_GPIO_Port, ENDSTOP_EP_H_NC_INT_Pin)) p[5] |= 0x04;
    if (HAL_GPIO_ReadPin(ENDSTOP_EP_L_NC_INT_GPIO_Port, ENDSTOP_EP_L_NC_INT_Pin)) p[5] |= 0x08;
    if (HAL_GPIO_ReadPin(ENDSTOP_SC_H_NC_INT_GPIO_Port, ENDSTOP_SC_H_NC_INT_Pin)) p[5] |= 0x10;

    /* --- Byte 6: Misc GPIO  (1 = pin HIGH) ---
     *   bit 0 = LED_OUT
     *   bit 1 = BlueButton
     */
    if (HAL_GPIO_ReadPin(LED_OUT_GPIO_Port,    LED_OUT_Pin))     p[6] |= 0x01;
    if (HAL_GPIO_ReadPin(BlueButton_GPIO_Port, BlueButton_Pin))  p[6] |= 0x02;

    /* --- Byte 7: Debug — ISR RX frame counter (wraps at 255) --- */
    p[7] = dbg_isr_count;

    FDCAN_SendFrame(CAN_ID_BCAST_GPIO, p, 8);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Helper: send Bcast_Config (0x606) with cached EEPROM startup defaults.
 *  Sent every periodic cycle alongside the other broadcast frames.
 * ═══════════════════════════════════════════════════════════════════════ */

static void Send_Config_Broadcast(void)
{
    const EEPROM_StartupConfig_t *cfg = EEPROM_GetCachedConfig();

    uint8_t p[6] = {0};
    p[0] = cfg->hs_state & 0x0FU;  /* bits 0-3: DR/E/SC/VBUCK defaults */
    p[1] = cfg->fan_dr;
    p[2] = cfg->fan_ep;
    p[3] = cfg->fan_eh;
    p[4] = cfg->fan_st;
    p[5] = cfg->fan_sf;

    FDCAN_SendFrame(CAN_ID_BCAST_CONFIG, p, 6);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Vector table remap  (bootloader compatibility)
 * ═══════════════════════════════════════════════════════════════════════ */

static void VectorBase_Config(void)
{
    extern const unsigned long g_pfnVectors[];
    SCB->VTOR = (unsigned long)&g_pfnVectors[0];
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════════════ */

void Pre_CAN_Handler_Init(void)
{
    VectorBase_Config();
}

void CAN_Handler_Init(void)
{
    /* Accept any extended frame whose bits[15:0] == our device ID (0x0667).
     * M_CAN mask: (RxID XOR FilterID1) AND FilterID2 == 0 → accept.
     *   mask bit 1 = must match FilterID1,  mask bit 0 = don't care.
     * 0x0000FFFF → bits[28:16] don't-care (any msg type), bits[15:0] must
     * match 0x0667. */
    FDCAN_FilterTypeDef filt = {0};
    filt.IdType       = FDCAN_EXTENDED_ID;
    filt.FilterIndex  = 0;
    filt.FilterType   = FDCAN_FILTER_MASK;
    filt.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filt.FilterID1    = CAN_DEVICE_ID;      /* match value: 0x0667           */
    filt.FilterID2    = 0x0000FFFFU;        /* mask: bits[15:0] must match   */

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filt) != HAL_OK)
        Error_Handler();

    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT, FDCAN_REJECT,
                                 FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
        Error_Handler();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CAN_Process  —  main-loop command handler
 *
 *  To add a new command:
 *    1. #define MSG_CMD_xxx in CAN_Handler.h
 *    2. Add a case below
 * ═══════════════════════════════════════════════════════════════════════ */

void CAN_Process(void)
{
    if (!rx_pending)
        return;

    /* Copy volatile data into local variables */
    uint32_t id   = rx_id;
    uint8_t  len  = rx_len;
    uint8_t  data[8];
    memcpy(data, (void *)rx_data, 8);
    rx_pending = false;

    /* Route by message type */
    uint16_t msg = CAN_MSG_TYPE(id);
    dbg_last_msg = msg;
    dbg_cmd_count++;

    switch (msg) {

    /* ── High-side power control  [0]=bitmask ─────────────────────────────
     *   bit0=Drive  bit1=Extruder  bit2=Scrubbing  bit3=12V Buck
     *   0=OFF  1=ON  (all four channels set in one frame)
     * ──────────────────────────────────────────────────────────────────── */
    case MSG_CMD_HS_POWER:
        if (len >= 1U) {
            uint8_t state = data[0];

            if (state & 0x01U) Enable_HighSide_Power_Module(HS_MODULE_DRIVE, 0U);
            else               Disable_HighSide_Power_Module(HS_MODULE_DRIVE, 0U);

            if (state & 0x02U) Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 0U);
            else               Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 0U);

            if (state & 0x04U) Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 0U);
            else               Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 0U);

            if (state & 0x08U) Enable_12V_Buck_Converter();
            else               Disable_12V_Buck_Converter();
        }
        break;

    /* ── Fan speed control  [0..4]=DR/EP/EH/ST/SF speed (0–100 %) ─────────
     *   All 5 fans are set in one frame.  Bytes beyond DLC keep last value.
     * ──────────────────────────────────────────────────────────────────── */
    case MSG_CMD_FAN_PWM:
        if (len >= 1U) set_Fan_PWM(FAN_DR, data[0]);
        if (len >= 2U) set_Fan_PWM(FAN_EP, data[1]);
        if (len >= 3U) set_Fan_PWM(FAN_EH, data[2]);
        if (len >= 4U) set_Fan_PWM(FAN_ST, data[3]);
        if (len >= 5U) set_Fan_PWM(FAN_SF, data[4]);
        break;

    /* ── EEPROM startup config ────────────────────────────────────────────
     *   Byte 0:  0 = load hard-coded safe defaults (all OFF) and apply
     *            1 = save current HS/fan state to EEPROM
     *   Config broadcast (0x606) fires on next periodic cycle.
     * ──────────────────────────────────────────────────────────────────── */
    case MSG_CMD_EEPROM:
        if (len >= 1U) {
            if (data[0] == 0x00U) {
                EEPROM_LoadAndApplyDefaults();  /* 0 = reset to hard-coded defaults */
            } else {
                EEPROM_SaveStartupConfig();     /* 1 = save current state → EEPROM  */
            }
        }
        break;

    default:
        /* Unknown command — ignore */
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CAN_Broadcast  —  periodic telemetry  (call from main loop)
 *
 *  The TX FIFO is only 3 elements deep, so we split the 7 broadcast
 *  frames into 3 phases (max 3 frames each), staggered by period_ms/3.
 *
 *    Phase 0: 0x600 Status  + 0x601 Currents + 0x602 Temps
 *    Phase 1: 0x603 Fans    + 0x604 GPIO     + 0x605 Raw ADC
 *    Phase 2: 0x606 Config
 * ═══════════════════════════════════════════════════════════════════════ */

int CAN_Broadcast(uint32_t period_ms)
{
    static uint32_t last_tick = 0;
    static uint8_t  phase     = 0;

    uint32_t now = HAL_GetTick();
    uint32_t interval = period_ms / 3U;   /* ≈167 ms per phase at 500 ms */

    if ((now - last_tick) < interval)
        return CAN_SUCCESS;
    last_tick = now;

    switch (phase) {

    case 0:
        /* ── 0x600  Status: voltages + endstop/ESTOP summary ───── */
        {
            uint8_t p[8] = {0};

            CAN_Packer_Endstop_and_ESTOP_2Byte(&p[0], &p[1], 2);
            CAN_Packer_24V_Bus_2Byte(&p[2], 2);
            CAN_Packer_12V_Bus_1Byte(&p[4], 1);
            p[5] = dbg_cmd_count;
            p[6] = (uint8_t)(dbg_last_msg & 0xFF);
            p[7] = (uint8_t)(dbg_last_msg >> 8);

            FDCAN_SendFrame(CAN_ID_BCAST_STATUS, p, 8);
        }
        /* ── 0x601  Currents ───────────────────────────────────── */
        {
            uint8_t p[5] = {0};

            CAN_Packer_24V_Bus_Current_1DP_2Byte(&p[0], 2);
            CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_DRIVE,     &p[2], 1);
            CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_EXTRUDER,  &p[3], 1);
            CAN_Packer_HighSide_Module_Current_1DP_1Byte(HS_MODULE_SCRUBBING, &p[4], 1);

            FDCAN_SendFrame(CAN_ID_BCAST_CURRENTS, p, 5);
        }
        /* ── 0x602  Temperatures ───────────────────────────────── */
        {
            uint8_t p[6] = {0};

            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_1, &p[0], 1);
            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_2, &p[1], 1);
            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_3, &p[2], 1);
            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_4, &p[3], 1);
            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_5, &p[4], 1);
            CAN_Packer_Thermistor_Temp_1Byte(TEMP_PTC_6, &p[5], 1);

            FDCAN_SendFrame(CAN_ID_BCAST_TEMPS, p, 6);
        }
        break;

    case 1:
        /* ── 0x603  Fan Speeds ─────────────────────────────────── */
        {
            uint8_t p[5] = {0};

            CAN_Packer_Fan_Speed_1Byte(FAN_DR, &p[0], 1);
            CAN_Packer_Fan_Speed_1Byte(FAN_EP, &p[1], 1);
            CAN_Packer_Fan_Speed_1Byte(FAN_EH, &p[2], 1);
            CAN_Packer_Fan_Speed_1Byte(FAN_ST, &p[3], 1);
            CAN_Packer_Fan_Speed_1Byte(FAN_SF, &p[4], 1);

            FDCAN_SendFrame(CAN_ID_BCAST_FANS, p, 5);
        }
        /* ── 0x604  GPIO Status ────────────────────────────────── */
        Send_GPIO_Status_Frame();

        /* ── 0x605  Raw ADC  (12 channels × 4 bits = 6 bytes) ─── */
        {
            extern volatile uint16_t ADC_VAL[];
            uint8_t p[6] = {0};

            for (int i = 0; i < 12; i++) {
                uint8_t nibble = (uint8_t)(ADC_VAL[i] >> 8);
                if (i % 2 == 0)
                    p[i / 2] |= (nibble << 4);
                else
                    p[i / 2] |= (nibble & 0x0F);
            }

            FDCAN_SendFrame(CAN_ID_BCAST_RAW_ADC, p, 6);
        }
        break;

    case 2:
        /* ── 0x606  Startup Config (EEPROM defaults) ───────────── */
        Send_Config_Broadcast();
        break;

    default:
        break;
    }

    phase++;
    if (phase > 2U)
        phase = 0U;

    return CAN_SUCCESS;
}
