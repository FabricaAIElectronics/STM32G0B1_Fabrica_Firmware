#include "Fan_PWM.h"
#include "main.h"
#include "tim.h"
#include <stdint.h>
#include "eeprom_driver.h"

/*🔴Preserve*/
typedef struct {
	TIM_HandleTypeDef *timer;
	uint32_t channel;
}PinSet_FanPwm;

// Declaration for FAN PWM Pins
PinSet_FanPwm FAN_PWM_1_DR;
PinSet_FanPwm FAN_PWM_2_EP;
PinSet_FanPwm FAN_PWM_3_EH;
PinSet_FanPwm FAN_PWM_4_ST;
PinSet_FanPwm FAN_PWM_5_SF;

void CAN_Handle_set_Fan_PWM(uint32_t id, uint8_t *params, uint8_t len)
{
	if (len < 2U)
		return;

	FanNumber_t fan_number = params[0];
	uint8_t speed_percent = params[1];

	set_Fan_PWM(fan_number, speed_percent);
}

//Define the actual instance for Fans 1 to 5
PinSet_FanPwm FAN_PWM_1_DR = {
		.timer = &htim1,
		.channel = TIM_CHANNEL_1
};
PinSet_FanPwm FAN_PWM_2_EP = {
		.timer = &htim1,
		.channel = TIM_CHANNEL_2
};
PinSet_FanPwm FAN_PWM_3_EH = {
		.timer = &htim1,
		.channel = TIM_CHANNEL_3
};
PinSet_FanPwm FAN_PWM_4_ST = {
		.timer = &htim1,
		.channel = TIM_CHANNEL_4
};
PinSet_FanPwm FAN_PWM_5_SF = {
		.timer = &htim14,
		.channel = TIM_CHANNEL_1
};


/*🟡Adapt*/
void set_Fan_PWM(FanNumber_t fan_number, uint8_t speed_percent)
{
	PinSet_FanPwm *fan = NULL;


	switch (fan_number)
	{
	case FAN_DR:
		fan = &FAN_PWM_1_DR;
		break;
	case FAN_EP:
		fan = &FAN_PWM_2_EP;
		break;
	case FAN_EH:
		fan = &FAN_PWM_3_EH;
		break;
	case FAN_ST:
		fan = &FAN_PWM_4_ST;
		break;
	case FAN_SF:
		fan = &FAN_PWM_5_SF;
		break;
	default:
		// invalid fan number: do nothing
		return;
	}

	if (fan == NULL || fan->timer == NULL)
	{
		return; 
	}

	// clamp speed to 0..100
	if (speed_percent > 100)
		speed_percent = 100;

	// read timer ARR (auto-reload) to scale percentage -> pulse
	uint32_t arr = fan->timer->Instance->ARR;

	// compute pulse value. For 100% -> arr, 0% -> 0
	uint32_t pulse = (arr * (uint32_t)speed_percent) / 100U;

	__HAL_TIM_SET_COMPARE(fan->timer, fan->channel, pulse);
}

// Start all configured fan PWM timers/channels.
// This will call HAL_TIM_PWM_Start for each fan pinset that has a valid timer.
/*🔴Preserve*/
void start_all_Fan_PWM(void)
{
	PinSet_FanPwm *fans[5] = { &FAN_PWM_1_DR, &FAN_PWM_2_EP, &FAN_PWM_3_EH, &FAN_PWM_4_ST, &FAN_PWM_5_SF };

	uint32_t i;
	for (i = 0; i < 5; ++i)
	{
		PinSet_FanPwm *f = fans[i];
		if (f == NULL || f->timer == NULL)
			continue;

		// Start PWM on the configured timer channel. HAL safely handles channel start.
		HAL_TIM_PWM_Start(f->timer, f->channel);
	}
}


/*🔴Preserve*/
typedef struct {
	TIM_HandleTypeDef *timer;
	uint32_t channel;
}PinSet_FanTacho;


PinSet_FanTacho FAN_TACHO_1_DR;
PinSet_FanTacho FAN_TACHO_2_EP;
PinSet_FanTacho FAN_TACHO_3_EH;
PinSet_FanTacho FAN_TACHO_4_ST;
PinSet_FanTacho FAN_TACHO_5_SF;

/*🔴Preserve*/
// Buffer for DMA capture of tacho pulses. Size should match number of tachos and expected captures per tacho.
static uint16_t DR_Fan_Tacho_Buffer[5]; 
static uint16_t EP_Fan_Tacho_Buffer[5];
static uint16_t EH_Fan_Tacho_Buffer[5];
static uint16_t ST_Fan_Tacho_Buffer[5];
static uint16_t SF_Fan_Tacho_Buffer[5];

/*🔴Preserve*/
//Define the actual instance for TACHO 1 to 5
PinSet_FanTacho FAN_TACHO_1_DR = {
		.timer = &htim2,
		.channel = TIM_CHANNEL_1
};
PinSet_FanTacho FAN_TACHO_2_EP = {
		.timer = &htim2,
		.channel = TIM_CHANNEL_2
};
PinSet_FanTacho FAN_TACHO_3_EH = {
		.timer = &htim2,
		.channel = TIM_CHANNEL_3
};
PinSet_FanTacho FAN_TACHO_4_ST = {
		.timer = &htim2,
		.channel = TIM_CHANNEL_4
};
PinSet_FanTacho FAN_TACHO_5_SF = {
		.timer = &htim3,
		.channel = TIM_CHANNEL_2
};

/*🔴Preserve*/
void start_Fan_Tacho_DMA(void)
{
	PinSet_FanTacho *Tachos[5] = { &FAN_TACHO_1_DR, &FAN_TACHO_2_EP, &FAN_TACHO_3_EH, &FAN_TACHO_4_ST, &FAN_TACHO_5_SF };

	HAL_TIM_IC_Start_DMA(Tachos[0]->timer, Tachos[0]->channel, (uint32_t*)DR_Fan_Tacho_Buffer, 5);
	HAL_TIM_IC_Start_DMA(Tachos[1]->timer, Tachos[1]->channel, (uint32_t*)EP_Fan_Tacho_Buffer, 5);
	HAL_TIM_IC_Start_DMA(Tachos[2]->timer, Tachos[2]->channel, (uint32_t*)EH_Fan_Tacho_Buffer, 5);
	HAL_TIM_IC_Start_DMA(Tachos[3]->timer, Tachos[3]->channel, (uint32_t*)ST_Fan_Tacho_Buffer, 5);
	HAL_TIM_IC_Start_DMA(Tachos[4]->timer, Tachos[4]->channel, (uint32_t*)SF_Fan_Tacho_Buffer, 5);
}

/*🔴Preserve*/
void Fan_Tacho_Speed_Calculate(FanNumber_t fan_number, uint16_t *speed_pct)
{
    uint16_t ticks = 0;

    switch (fan_number)
    {
    case FAN_DR:
        ticks = DR_Fan_Tacho_Buffer[1] - DR_Fan_Tacho_Buffer[0];
        break;
    case FAN_EP:
        ticks = EP_Fan_Tacho_Buffer[1] - EP_Fan_Tacho_Buffer[0];
        break;
    case FAN_EH:
        ticks = EH_Fan_Tacho_Buffer[1] - EH_Fan_Tacho_Buffer[0];
        break;
    case FAN_ST:
        ticks = ST_Fan_Tacho_Buffer[1] - ST_Fan_Tacho_Buffer[0];
        break;
    case FAN_SF:
        ticks = SF_Fan_Tacho_Buffer[1] - SF_Fan_Tacho_Buffer[0];
        break;
    default:
        *speed_pct = 0;
        return;
    }

    /* guard against division by zero (no pulses / fan stopped) */
    if (ticks == 0) {
        *speed_pct = 0;
        return;
    }

    /* Get fan_max_rpm from EEPROM cached config */
    const Config *cfgp = EEPROM_GetCachedConfig();
    uint16_t fan_max_rpm = 5000; /* fallback default */
    if (cfgp != NULL && cfgp->fan_max_rpm > 0) {
        fan_max_rpm = cfgp->fan_max_rpm;
    }

    /*
     * Timer clock = 800 kHz (prescaled from 8 MHz by PSC=9 or similar).
     * ticks = capture difference = period of one tacho pulse in timer counts.
     * frequency_hz = 800000 / ticks
     * RPM = (frequency_hz * 60) / pulses_per_rev
     *     = (frequency_hz * 30)   [for 2 pulses per revolution]
     */
    uint32_t frequency_hz = 800000U / (uint32_t)ticks;
    uint32_t rpm = (frequency_hz * 30U);

    /* Convert RPM to percentage of max, clamped to 100% */
    uint32_t pct = (rpm * 100U) / (uint32_t)fan_max_rpm;
    if (pct > 100U) pct = 100U;

    *speed_pct = (uint16_t)pct;
}


size_t CAN_Packer_Fan_Speed_1Byte(FanNumber_t fan_number, uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size < 1) return 0;

    uint16_t speed_pct = 0;
    Fan_Tacho_Speed_Calculate(fan_number, &speed_pct);

    out[0] = (uint8_t)speed_pct;
    return 1;
}
