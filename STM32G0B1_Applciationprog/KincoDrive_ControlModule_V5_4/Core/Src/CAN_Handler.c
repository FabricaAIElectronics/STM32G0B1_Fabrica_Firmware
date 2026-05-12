/**
 * @file    CAN_Handler.c
 * @brief   CAN command dispatch + telemetry broadcast.
 *
 * @details ISR  →  saves last received frame into a single-slot global
 *          Main →  CAN_Process() reads the global, runs a switch-case
 *          Main →  CAN_Broadcast() emits 7 telemetry frames every 500 ms,
 *                                  staggered across 3 phases.
 *
 *          To add a command:
 *            1. #define CAN_ID_CMD_xxx in CAN_Handler.h (in 0x110..0x12F)
 *            2. Add a case below in CAN_Process()
 *
 * @author  jordan
 * @date    2026-05-06
 */

#include "CAN_Handler.h"
#include "main.h"
#include "stm32g0xx.h"
#include <string.h>
#include <stdbool.h>

#include "Fan_PWM.h"
#include "adc_driver.h"
#include "hs_switch.h"
#include "power_monitor.h"
#include "thermistor.h"
#include "eeprom_driver.h"
#include "applogic.h"

extern FDCAN_HandleTypeDef hfdcan1;
extern AppStateMachine     app_sm;     /* defined in main.c */

/* ════════════════════════════════════════════════════════════════════════
 *  ISR → main-loop message passing  (single-slot, no queue)
 * ════════════════════════════════════════════════════════════════════════ */

static volatile bool     rx_pending = false;
static volatile uint32_t rx_id;
static volatile uint8_t  rx_data[8];
static volatile uint8_t  rx_len;

/* Map an FDCAN_DLC_BYTES_x constant back to its byte count (0..8). */
static uint8_t dlc_to_bytes(uint32_t dlc_code)
{
    /* The HAL stores raw 4-bit DLC; for classic CAN it equals byte count. */
    if (dlc_code <= FDCAN_DLC_BYTES_8) {
        return (uint8_t)(dlc_code & 0x0F); /* HAL constants are bit-positioned */
    }
    return 8U;
}

/* Send a 11-bit standard frame on FDCAN1. */
static bool fdcan_send_std(uint16_t std_id, const uint8_t *data, uint8_t len)
{
    static const uint32_t lut[9] = {
        FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
        FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
        FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8
    };

    if (len > 8U) len = 8U;

    FDCAN_TxHeaderTypeDef hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.Identifier          = std_id & 0x7FFU;
    hdr.IdType              = FDCAN_STANDARD_ID;
    hdr.TxFrameType         = FDCAN_DATA_FRAME;
    hdr.DataLength          = lut[len];
    hdr.BitRateSwitch       = FDCAN_BRS_OFF;
    hdr.FDFormat            = FDCAN_CLASSIC_CAN;
    hdr.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;

    uint8_t tx_buf[8] = {0};
    if (data && len) memcpy(tx_buf, data, len);

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &hdr, tx_buf) == HAL_OK;
}

/* ════════════════════════════════════════════════════════════════════════
 *  FDCAN RX FIFO0 callback (ISR context)
 * ════════════════════════════════════════════════════════════════════════ */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if (!(RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE)) return;

    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &hdr, data) != HAL_OK)
        return;

    /* Bootloader entry: XCP CONNECT (byte[0]=0xFF, dlc=2) on bootloader RX ID. */
    if ((hdr.Identifier == CAN_ID_BOOTLOADER) &&
        (data[0] == 0xFF) &&
        (hdr.DataLength == FDCAN_DLC_BYTES_2)) {
        HAL_NVIC_SystemReset();
    }

    /* Save frame for main-loop processing */
    uint8_t dlc = dlc_to_bytes(hdr.DataLength);
    if (dlc > 8U) dlc = 8U;
    memset((void *)rx_data, 0, 8);
    memcpy((void *)rx_data, data, dlc);
    rx_len = dlc;
    rx_id  = hdr.Identifier;
    __DMB();
    rx_pending = true;
}

/* ════════════════════════════════════════════════════════════════════════
 *  Vector table remap (bootloader compatibility)
 * ════════════════════════════════════════════════════════════════════════ */


/* ════════════════════════════════════════════════════════════════════════
 *  Initialization — RX filter and notifications
 * ════════════════════════════════════════════════════════════════════════ */

void CAN_Handler_Init(void)
{
    /* Accept the KincoDrive standard-ID sub-block 0x101..0x12F. */
    FDCAN_FilterTypeDef filt = {0};
    filt.IdType       = FDCAN_STANDARD_ID;
    filt.FilterIndex  = 0;
    filt.FilterType   = FDCAN_FILTER_RANGE;
    filt.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filt.FilterID1    = CAN_ID_RANGE_LOW;
    filt.FilterID2    = CAN_ID_RANGE_HIGH;

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filt) != HAL_OK) Error_Handler();

    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT, FDCAN_REJECT,
                                 FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) Error_Handler();
}

/* ════════════════════════════════════════════════════════════════════════
 *  CAN_Process — main-loop command handler
 * ════════════════════════════════════════════════════════════════════════ */

void CAN_Process(void)
{
    if (!rx_pending) return;

    uint32_t id = rx_id;
    uint8_t  len = rx_len;
    uint8_t  data[8];
    memcpy(data, (void *)rx_data, 8);
    rx_pending = false;

    switch (id) {

    /* ── 0x110 CMD_HS_POWER  byte[0] bitmask ─────────────────────────────
     *   bit0=Drive  bit1=Extruder  bit2=Scrubbing  bit3=12V Buck
     *   1=ENABLE  (also clears any stale OC error for that channel)
     *   0=DISABLE
     * ──────────────────────────────────────────────────────────────────── */
    case CAN_ID_CMD_HS_POWER:
        if (len >= 1U) {
            uint8_t s = data[0];

            if (s & 0x01U) { PM_Clear_OC_Error(HS_MODULE_DRIVE);     HS_Enable(HS_MODULE_DRIVE); }
            else                                                      HS_Disable(HS_MODULE_DRIVE);

            if (s & 0x02U) { PM_Clear_OC_Error(HS_MODULE_EXTRUDER);  HS_Enable(HS_MODULE_EXTRUDER); }
            else                                                      HS_Disable(HS_MODULE_EXTRUDER);

            if (s & 0x04U) { PM_Clear_OC_Error(HS_MODULE_SCRUBBING); HS_Enable(HS_MODULE_SCRUBBING); }
            else                                                      HS_Disable(HS_MODULE_SCRUBBING);

            if (s & 0x08U) Buck12V_Enable();
            else           Buck12V_Disable();
        }
        break;

    /* ── 0x111 CMD_FAN_PWM  byte[0..4] = DR/EP/EH/ST/SF  (0–100 %) ───── */
    case CAN_ID_CMD_FAN_PWM:
        if (len >= 1U) set_Fan_PWM(FAN_DR, data[0]);
        if (len >= 2U) set_Fan_PWM(FAN_EP, data[1]);
        if (len >= 3U) set_Fan_PWM(FAN_EH, data[2]);
        if (len >= 4U) set_Fan_PWM(FAN_ST, data[3]);
        if (len >= 5U) set_Fan_PWM(FAN_SF, data[4]);
        break;

    /* ── 0x112 CMD_EEPROM  byte[0]: 0=load defaults, 1=save current ── */
    case CAN_ID_CMD_EEPROM:
        if (len >= 1U) {
            if (data[0] == 0x00U) EEPROM_LoadAndApplyDefaults();
            else                  EEPROM_SaveStartupConfig();
        }
        break;

    /* ── 0x113 CMD_OC_THRESHOLD ───────────────────────────────────────
     *   byte[0..1] = OC_DR  (uint16 LE, mA)
     *   byte[2..3] = OC_EXT (uint16 LE, mA)
     *   byte[4..5] = OC_SC  (uint16 LE, mA)
     *   Update is RAM-only; persist with CMD_EEPROM (0x112) byte[0]=1.
     * ─────────────────────────────────────────────────────────────────── */
    case CAN_ID_CMD_OC_THRESHOLD:
        if (len >= 2U) PM_Set_OC_Threshold(HS_MODULE_DRIVE,     (uint16_t)((uint16_t)data[1] << 8 | data[0]));
        if (len >= 4U) PM_Set_OC_Threshold(HS_MODULE_EXTRUDER,  (uint16_t)((uint16_t)data[3] << 8 | data[2]));
        if (len >= 6U) PM_Set_OC_Threshold(HS_MODULE_SCRUBBING, (uint16_t)((uint16_t)data[5] << 8 | data[4]));
        break;

    /* ── 0x114 CMD_UV_THRESHOLD ───────────────────────────────────────
     *   byte[0..1] = UV_24V (uint16 LE, mV)
     *   byte[2..3] = UV_12V (uint16 LE, mV)
     * ─────────────────────────────────────────────────────────────────── */
    case CAN_ID_CMD_UV_THRESHOLD:
        if (len >= 2U) PM_Set_UV_24V_Threshold((uint16_t)((uint16_t)data[1] << 8 | data[0]));
        if (len >= 4U) PM_Set_UV_12V_Threshold((uint16_t)((uint16_t)data[3] << 8 | data[2]));
        break;

    default:
        /* Unknown ID → ignore */
        break;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 *  Telemetry packers (private helpers)
 * ════════════════════════════════════════════════════════════════════════ */

static inline void pack_u16_le(uint8_t *out, uint16_t v)
{
    out[0] = (uint8_t)(v & 0xFFU);
    out[1] = (uint8_t)(v >> 8);
}

static uint16_t clamp_u16(uint32_t v)
{
    return (v > 0xFFFFU) ? 0xFFFFU : (uint16_t)v;
}

/* ── 0x120 BCAST_STATUS (8B) ──
 *   byte[0..1] V24_mV    LE
 *   byte[2..3] V12_mV    LE
 *   byte[4..5] I_24V_mA  LE
 *   byte[6]    sys_state (0=INIT 1=LOAD_CFG 2=RUNNING)
 *   byte[7]    error_mask (ERR_OC_xxx | ERR_UV_xxx)
 */
static void send_bcast_status(void)
{
    uint8_t p[8] = {0};
    pack_u16_le(&p[0], clamp_u16(PM_Read_24V_Voltage_mV()));
    pack_u16_le(&p[2], clamp_u16(PM_Read_12V_Voltage_mV()));
    pack_u16_le(&p[4], clamp_u16(PM_Read_24V_Bus_Current_mA()));
    p[6] = (uint8_t)AppLogic_GetState(&app_sm);
    p[7] = PM_GetErrorMask();
    fdcan_send_std(CAN_ID_BCAST_STATUS, p, 8);
}

/* ── 0x121 BCAST_CURRENTS (8B) — bus + per-HS currents (mA, LE uint16) ── */
static void send_bcast_currents(void)
{
    uint8_t p[8] = {0};
    pack_u16_le(&p[0], clamp_u16(PM_Read_24V_Bus_Current_mA()));
    pack_u16_le(&p[2], clamp_u16(PM_Read_HS_Current_mA(HS_MODULE_DRIVE)));
    pack_u16_le(&p[4], clamp_u16(PM_Read_HS_Current_mA(HS_MODULE_EXTRUDER)));
    pack_u16_le(&p[6], clamp_u16(PM_Read_HS_Current_mA(HS_MODULE_SCRUBBING)));
    fdcan_send_std(CAN_ID_BCAST_CURRENTS, p, 8);
}

/* ── 0x122 BCAST_TEMPS (6B) — 6× PTC, int8 with +40 offset (-40..+215 °C) ── */
static void send_bcast_temps(void)
{
    static const ADC_Channel_t chans[6] = {
        TEMP_PTC_1, TEMP_PTC_2, TEMP_PTC_3,
        TEMP_PTC_4, TEMP_PTC_5, TEMP_PTC_6,
    };
    uint8_t p[6] = {0};
    for (int i = 0; i < 6; i++) {
        int32_t t_C = -40;
        Thermistor_Read_C(chans[i], &t_C);
        int32_t enc = t_C + 40;
        if (enc < 0)   enc = 0;
        if (enc > 255) enc = 255;
        p[i] = (uint8_t)enc;
    }
    fdcan_send_std(CAN_ID_BCAST_TEMPS, p, 6);
}

/* ── 0x123 BCAST_FANS (5B) — fan tach % per channel ── */
static void send_bcast_fans(void)
{
    static const FanNumber_t fans[5] = { FAN_DR, FAN_EP, FAN_EH, FAN_ST, FAN_SF };
    uint8_t p[5] = {0};
    for (int i = 0; i < 5; i++) {
        uint16_t pct = 0;
        Fan_Tacho_Speed_Calculate(fans[i], &pct);
        p[i] = (uint8_t)((pct > 100U) ? 100U : pct);
    }
    fdcan_send_std(CAN_ID_BCAST_FANS, p, 5);
}

/* ── 0x124 BCAST_GPIO (8B) — full raw GPIO state (no interpretation) ──
 *   byte[0] HS EN bits         bit0=DR bit1=E bit2=SC bit3=VBUCK
 *   byte[1] HS PG bits         bit0=DR bit1=E bit2=SC  (1 = PGOOD asserted)
 *   byte[2] HS FT bits         bit0=DR bit1=E bit2=SC  (1 = FAULT asserted)
 *   byte[3] Misc IN raw bits   bit0=B1 bit1=Toggle_Pos_Detect
 *   byte[4] Misc OUT raw bits  bit0=LED2
 *   byte[5..7] reserved
 */
static void send_bcast_gpio(void)
{
    uint8_t p[8] = {0};

    if (HS_IsEnabled(HS_MODULE_DRIVE))     p[0] |= 0x01U;
    if (HS_IsEnabled(HS_MODULE_EXTRUDER))  p[0] |= 0x02U;
    if (HS_IsEnabled(HS_MODULE_SCRUBBING)) p[0] |= 0x04U;
    if (Buck12V_IsEnabled())               p[0] |= 0x08U;

    if (HS_PowerGood(HS_MODULE_DRIVE))     p[1] |= 0x01U;
    if (HS_PowerGood(HS_MODULE_EXTRUDER))  p[1] |= 0x02U;
    if (HS_PowerGood(HS_MODULE_SCRUBBING)) p[1] |= 0x04U;

    if (HS_FaultPin(HS_MODULE_DRIVE))      p[2] |= 0x01U;
    if (HS_FaultPin(HS_MODULE_EXTRUDER))   p[2] |= 0x02U;
    if (HS_FaultPin(HS_MODULE_SCRUBBING))  p[2] |= 0x04U;

    if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin))                                   p[3] |= 0x01U;
    if (HAL_GPIO_ReadPin(Toggle_Pos_Detect_GPIO_Port, Toggle_Pos_Detect_Pin))     p[3] |= 0x02U;

    if (HAL_GPIO_ReadPin(LED2_GPIO_Port, LED2_Pin))                               p[4] |= 0x01U;

    fdcan_send_std(CAN_ID_BCAST_GPIO, p, 8);
}

/* ── 0x125 BCAST_RAW_ADC (6B) — 12 channels, 4-bit nibbles (raw >> 8) ── */
static void send_bcast_raw_adc(void)
{
    uint8_t p[6] = {0};
    for (int i = 0; i < 12; i++) {
        uint8_t nibble = (uint8_t)(ADC_VAL[i] >> 8);
        if (i % 2 == 0) p[i / 2] |= (uint8_t)(nibble << 4);
        else            p[i / 2] |= (nibble & 0x0FU);
    }
    fdcan_send_std(CAN_ID_BCAST_RAW_ADC, p, 6);
}

/* ── 0x126 BCAST_CONFIG_A (8B) — HS state + per-channel OC thresholds ──
 *   byte[0]    hs_state bitmask (matches CAN_ID_CMD_HS_POWER format)
 *   byte[1..2] OC_DR  mA  (LE)
 *   byte[3..4] OC_EXT mA  (LE)
 *   byte[5..6] OC_SC  mA  (LE)
 *   byte[7]    reserved
 */
static void send_bcast_config_a(void)
{
    const EEPROM_StartupConfig_t *cfg = EEPROM_GetCachedConfig();
    uint8_t p[8] = {0};
    p[0] = cfg->hs_state & 0x0FU;
    pack_u16_le(&p[1], cfg->oc_dr_mA);
    pack_u16_le(&p[3], cfg->oc_e_mA);
    pack_u16_le(&p[5], cfg->oc_sc_mA);
    fdcan_send_std(CAN_ID_BCAST_CONFIG_A, p, 8);
}

/* ── 0x127 BCAST_CONFIG_B (8B) — UV thresholds + fan defaults from EEPROM ──
 *   byte[0..1] UV_24V mV (LE)
 *   byte[2..3] UV_12V mV (LE)
 *   byte[4]    fan_dr_default
 *   byte[5]    fan_ep_default
 *   byte[6]    fan_eh_default
 *   byte[7]    fan_st_default            (fan_sf default omitted — use BCAST_FANS for live)
 */
static void send_bcast_config_b(void)
{
    const EEPROM_StartupConfig_t *cfg = EEPROM_GetCachedConfig();
    uint8_t p[8] = {0};
    pack_u16_le(&p[0], cfg->uv_24V_mV);
    pack_u16_le(&p[2], cfg->uv_12V_mV);
    p[4] = cfg->fan_dr;
    p[5] = cfg->fan_ep;
    p[6] = cfg->fan_eh;
    p[7] = cfg->fan_st;
    fdcan_send_std(CAN_ID_BCAST_CONFIG_B, p, 8);
}

/* ════════════════════════════════════════════════════════════════════════
 *  CAN_Broadcast — periodic telemetry, 3-phase staggered
 *
 *  TX FIFO is 3 slots deep. We split 7 frames across 3 phases and stagger
 *  by period_ms/3 so we never overflow.
 *    Phase 0: 0x120 STATUS  + 0x121 CURRENTS + 0x122 TEMPS
 *    Phase 1: 0x123 FANS    + 0x124 GPIO     + 0x125 RAW_ADC
 *    Phase 2: 0x126 CONFIG
 * ════════════════════════════════════════════════════════════════════════ */

int CAN_Broadcast(uint32_t period_ms)
{
    static uint32_t last_tick = 0;
    static uint8_t  phase     = 0;

    uint32_t now      = HAL_GetTick();
    uint32_t interval = period_ms / 3U;

    if ((now - last_tick) < interval) return CAN_SUCCESS;
    last_tick = now;

    switch (phase) {
    case 0:
        send_bcast_status();
        send_bcast_currents();
        send_bcast_temps();
        break;
    case 1:
        send_bcast_fans();
        send_bcast_gpio();
        send_bcast_raw_adc();
        break;
    case 2:
        send_bcast_config_a();
        send_bcast_config_b();
        break;
    default:
        break;
    }

    if (++phase > 2U) phase = 0U;
    return CAN_SUCCESS;
}
