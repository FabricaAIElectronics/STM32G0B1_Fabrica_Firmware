/*
 * io_module.h
 *
 *  Created on: 6 Feb 2026
 *      Author: jordan
 */

#ifndef INC_IO_MODULE_H_
#define INC_IO_MODULE_H_



#include "main.h"
#include "stdbool.h"

/* TPS2493 current monitor — ADC to mV-at-sense conversion.
 *
 * Theoretical: VRef / (ADC_MAX x AMON_typ) x 1000
 *   = 3.3 / (4096 x 48) x 1000 = 1.678e-2
 *
 * TPS2493 AMON tolerance: 32–64 V/V (typ 48).
 * Calibrated against load-cell: 745 mA actual read as 1100 mA
 *   correction = 745/1100 = 0.6773
 *   K_SCALE = 1.846e-2 x 0.6773 ≈ 1.250e-2                    */
#define K_SCALE     1.250e-2f
#define RSENSE      0.003f      /* 3 mΩ  — all rails except LED  */
#define RSENSELED   0.020f      /* 20 mΩ — LED rail              */

/* ------------------------------------------------------------------
 * Bus-voltage scale factors (mV per ADC count).
 *
 * Theoretical for a divider of ratio N (= (R_top + R_bot)/R_bot) with
 * 12-bit ADC and Vref = 3.3 V:
 *
 *     mV / count = (3300 / 4095) * N ≈ 0.806 * N
 *
 * Calibration procedure (per rail):
 *   1. Power the rail to a known voltage (multimeter at the device pad).
 *   2. Read displayed value on the OLED or BCAST_VOLTAGE.
 *   3. K_NEW = K_OLD × (V_actual / V_reported)
 *
 * Calibration log:
 *   V24:  reported 22.20 V at actual 23.62 V (multimeter)
 *           → K = 6.286 × (23.62 / 22.20) = 6.689
 *   VCAP: not yet calibrated — start with 6.286, tune from a multimeter read.
 *   V12:  not yet calibrated — same, lower nominal voltage so easier to verify.
 * ------------------------------------------------------------------ */
#define K_V24_MV_PER_COUNT      6.689f
#define K_VCAP_MV_PER_COUNT     6.289f
#define K_V12_MV_PER_COUNT      6.289f
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} GPIO_Pin_t;

typedef enum {
	RAIL_AUX,
	RAIL_LED,
	RAIL_DRIVE,
	RAIL_CAP,
	RAIL_SBC,
	RAIL_COUNT
}PowerRailIndex_t;

typedef struct {
    GPIO_Pin_t enable;
    GPIO_Pin_t fault;
    GPIO_Pin_t pgood;
} HS_CTRL_;


#define ADC_CHANNELS 10
/* DMA is configured for WORD (32-bit) transfers in CubeMX —
 * buffer MUST be uint32_t to match DMA_MDATAALIGN_WORD.
 * ADC value sits in the lower 12 bits of each 32-bit word. */
extern volatile uint32_t adc_buffer[ADC_CHANNELS];

// ADC channel indices
typedef enum {
    ADC_CURR_BAT = 0,
    ADC_CURR_CAP,
    ADC_CURR_SBC,
    ADC_CURR_DRIVE,
    ADC_CURR_AUX,
    ADC_CURR_LED,
    ADC_V_24,
    ADC_V_CAP,
    ADC_V_12,
    ADC_V_NTC
} ADC_ChannelIndex_t;

typedef struct {
	struct {
		uint16_t _currbat;
		uint16_t _currcap;
		uint16_t _currsbc;
		uint16_t _currdrive;
		uint16_t _curraux;
		uint16_t _currled;
	}current_mA;

	struct {
		uint16_t V24;
		uint16_t VCAP;
		uint16_t V12;
	}voltage_mV;

	float   NTCTemperature_C;

	/* 6S Li-ion / Li-Po pack state-of-charge in percent (0..100).
	 * Populated by Battery_EstimateSOC_pct() at the end of Run_Measurements().
	 * See battery.h for the OCV lookup + IR-compensation algorithm. */
	uint8_t battery_soc_pct;
}SystemMeasurement_t;

extern SystemMeasurement_t measurements;
extern HS_CTRL_ hotswap[RAIL_COUNT];


void HS_init(void);
void HS_Enable(HS_CTRL_ *HS);
void HS_EnableAll();
void HS_EnableWithoutCap();
void HS_Disable(HS_CTRL_ *HS);
void HS_DisableAll();
bool HS_Fault(HS_CTRL_ *HS);
bool HS_PGood(HS_CTRL_ *HS);

void Bat_curr_measurement(SystemMeasurement_t *ms);
void Cap_curr_measurement(SystemMeasurement_t *ms);
void Drive_curr_measurement(SystemMeasurement_t *ms);
void LED_curr_measurement(SystemMeasurement_t *ms);
void SBC_curr_measurement(SystemMeasurement_t *ms);
void AUX_curr_measurement(SystemMeasurement_t *ms);
void NTC_Temp_measurement(SystemMeasurement_t *ms);

void V24_volt_measurement(SystemMeasurement_t *ms);
void VCAP_volt_measurement(SystemMeasurement_t *ms);
void V12_volt_measurement(SystemMeasurement_t *ms);
#endif /* INC_IO_MODULE_H_ */
