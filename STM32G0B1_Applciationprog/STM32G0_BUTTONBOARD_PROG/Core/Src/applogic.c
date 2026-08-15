/**
  ******************************************************************************
  * @file    applogic.c
  * @brief   Top-level state machine.
  *
  *   INIT ──100 ms──> LOAD_CONFIG ──> RUNNING
  *                                      │
  *                                      └─ ERROR only if the CAN peripheral
  *                                         itself failed; everything else is
  *                                         reported in the error mask and the
  *                                         board keeps working.
  *
  * The STM32F303 knob build sent every fault to a latched ERROR state that
  * self-reset after two seconds. That made sense there, where a bad encoder
  * reading meant the board could not do its one job. Here the inputs are
  * independent: a dead rotary contact must not stop the ten buttons and ten
  * LEDs from working, so faults are advisory.
  ******************************************************************************
  */

#include "applogic.h"
#include "i2c_host.h"
#include <string.h>

static void enter_state(AppStateMachine *sm, AppState next)
{
    sm->state = next;
    sm->stateEntryTick = HAL_GetTick();
}

static void apply_config(AppStateMachine *sm)
{
    Leds_Set(sm->config.default_led_mask, sm->config.default_led_int_mask);
    Leds_SelectSource((sm->config.default_led_source != 0U) ? LED_SRC_STM32
                                                            : LED_SRC_DIRECT);
}

/* --------------------------------------------------------------------------
 * Host commands
 * --------------------------------------------------------------------------
 * Each flag is set by the FDCAN RX ISR and cleared here. Clearing BEFORE
 * acting would open a window where a command arriving mid-service is lost;
 * clearing after means a command arriving mid-service is simply serviced
 * again on the next pass, which is harmless for every command here because
 * they are all idempotent level-sets rather than increments.
 */
static void service_commands(AppStateMachine *sm)
{
    if (can_rxMessage.led_update != 0U) {
        Leds_Set(can_rxMessage.led_mask, can_rxMessage.led_int_mask);
        can_rxMessage.led_update = 0U;
    }

    if (can_rxMessage.buffer_update != 0U) {
        Leds_SelectSource((LedSource)can_rxMessage.buffer_source);
        can_rxMessage.buffer_update = 0U;
    }

    if (can_rxMessage.encoder_preset_update != 0U) {
        Inputs_SetEncoder(&sm->inputs, can_rxMessage.encoder_preset);
        can_rxMessage.encoder_preset_update = 0U;
    }

    if (can_rxMessage.eeprom_load_defaults != 0U) {
        LoadDefault(&sm->config);
        apply_config(sm);
        sm->errorMask &= (uint8_t)~ERR_EEPROM;
        can_rxMessage.eeprom_load_defaults = 0U;
    }

    if (can_rxMessage.eeprom_save != 0U) {
        /* Snapshot what is on the panel right now as the new power-on state,
         * which is what an operator means by "save": they have set the board
         * up by hand and want it to come back this way. */
        sm->config.magic                = EEPROM_CFG_MAGIC;
        sm->config.default_led_mask     = Leds_GetMask();
        sm->config.default_led_int_mask = Leds_GetIntMask();
        sm->config.default_led_source   = (uint8_t)Leds_GetSource();
        EEPROM_Write_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET,
                            &sm->config);
        can_rxMessage.eeprom_save = 0U;
    }
}

/* Low nibble of KNOBSTATE byte 6: the actual pin levels of the two output
 * enables, inverted to read as "this bank is enabled". Reporting the live
 * pins rather than the requested source is deliberate — if the two ever
 * disagree, the host sees the truth. */
static uint8_t buff_sel_nibble(void)
{
    uint8_t n = 0U;
    if (HAL_GPIO_ReadPin(BUFF_SEL_1_GPIO_Port, BUFF_SEL_1_Pin) == GPIO_PIN_RESET) {
        n |= 0x01U;
    }
    if (HAL_GPIO_ReadPin(BUFF_SEL_2_GPIO_Port, BUFF_SEL_2_Pin) == GPIO_PIN_RESET) {
        n |= 0x02U;
    }
    return n;
}

static void update_error_mask(AppStateMachine *sm)
{
    if (sm->inputs.rotary_fault) {
        sm->errorMask |= (uint8_t)ERR_ROTARY_INVALID;
    } else {
        sm->errorMask &= (uint8_t)~ERR_ROTARY_INVALID;
    }
}

static void broadcast_fast(AppStateMachine *sm)
{
    CAN_BroadcastKnobState(&sm->inputs, (uint8_t)sm->state, buff_sel_nibble());
    CAN_BroadcastButtons(&sm->inputs);
}

static void broadcast_rotating(AppStateMachine *sm)
{
    switch (sm->rotatePhase) {
    case 0U:
        CAN_BroadcastLedState(Leds_GetMask(), Leds_GetIntMask(),
                              (uint8_t)Leds_GetSource());
        break;
    case 1U:
        CAN_BroadcastDeviceStatus((uint8_t)sm->state, sm->errorMask);
        break;
    default:
        CAN_BroadcastEepromData(&sm->config);
        break;
    }
    sm->rotatePhase = (uint8_t)((sm->rotatePhase + 1U) % 3U);
}

void AppLogic_Init(AppStateMachine *sm)
{
    memset(sm, 0, sizeof(*sm));
    sm->errorMask = (uint8_t)ERR_NONE;

    Inputs_Init(&sm->inputs);
    enter_state(sm, STATE_INIT);
}

void AppLogic_Run(AppStateMachine *sm)
{
    const uint32_t now = HAL_GetTick();

    /* Keep sampling in every state. The debouncers need a steady stream of
     * samples to converge, and a host watching during startup should see
     * real input data rather than zeros. */
    Inputs_Poll(&sm->inputs);
    I2CHost_Publish(&sm->inputs);
    CAN_FlashDetectionTick();

    switch (sm->state) {

    case STATE_INIT:
        /* Let the 5 V rail, the translators and the switch contacts settle
         * before the first EEPROM transaction. */
        if ((now - sm->stateEntryTick) >= TICK_INIT_MS) {
            enter_state(sm, STATE_LOAD_CONFIG);
        }
        break;

    case STATE_LOAD_CONFIG:
        EEPROM_Read_Config(EEPROM_CONFIG_PAGE, EEPROM_CONFIG_OFFSET,
                           &sm->config);
        if (!checkcfg(&sm->config)) {
            /* Blank or stale device — fall back to defaults and say so. Not
             * fatal: a board with no EEPROM fitted still has to work. */
            LoadDefault(&sm->config);
            sm->errorMask |= (uint8_t)ERR_EEPROM;
        }
        apply_config(sm);
        sm->lastFastTick   = now;
        sm->lastRotateTick = now;
        enter_state(sm, STATE_RUNNING);
        break;

    case STATE_RUNNING:
        service_commands(sm);
        update_error_mask(sm);

        /* Stay silent while a firmware download is in progress. XCP needs the
         * bus, and this board's telemetry is worth nothing during a flash. */
        if (can_rxMessage.flashdetected == 0U) {
            if ((now - sm->lastFastTick) >= TICK_FAST_MS) {
                sm->lastFastTick = now;
                broadcast_fast(sm);
            }
            if ((now - sm->lastRotateTick) >= TICK_ROTATE_MS) {
                sm->lastRotateTick = now;
                broadcast_rotating(sm);
            }
        }
        break;

    case STATE_ERROR:
    default:
        /* Only reachable if CAN itself failed, in which case the retry below
         * is the only thing that can help. */
        if ((now - sm->stateEntryTick) >= TICK_ERROR_RETRY_MS) {
            NVIC_SystemReset();
        }
        break;
    }
}
