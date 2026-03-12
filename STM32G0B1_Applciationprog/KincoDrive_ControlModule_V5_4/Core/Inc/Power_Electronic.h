#ifndef POWER_ELECTRONIC_H_
#define POWER_ELECTRONIC_H_

#include <stdint.h>
#include "main.h"
#include "stm32g0b1xx.h"

#define HS_CURRENT_SENSE_RESISTOR_MOHMS 3
#define PE_ERR_TIMEOUT     -1
#define PE_ERR_INVALID_PARAM -2
#define PE_ERR_GEN         -3
#define PE_SUCCESS          0

/*
🟡 Adapt
*/
typedef enum {
    HS_MODULE_DRIVE = 1,
    HS_MODULE_EXTRUDER = 2,
    HS_MODULE_SCRUBBING = 3
} HighSide_Module_t;

/*
🔴Preserve: Order is highly important, linked to the way the ADC scans and updates the DMA buffer
*/
/*Actual Enum for ADC data*/
typedef enum {
    TEMP_PTC_1 = 0,
    TEMP_PTC_2 = 1,
    TEMP_PTC_3 = 2,
    TEMP_PTC_4 = 3,
    TEMP_PTC_5 = 4,
    TEMP_PTC_6 = 5,
    CURR_MON_1 = 6,
    CURR_MON_2 = 7,
    CURR_MON_3 = 8,
    VADC_24 = 9,
    VADC_12 = 10,
    CURR_MON_24V = 11,
    ADC_BUF_LEN = 12

} ADC_Peripheral_t;


/* Enable high-side power for a module and wait up to timeout_ms for
   the module power-good (PG) signal if available.
   Returns 0 on success, -1 on timeout/failure, -2 on invalid parameter. */
int Enable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);

int Disable_HighSide_Power_Module(HighSide_Module_t module, uint32_t timeout_ms);

int Enable_HighSide_Power_All(uint32_t timeout_ms);

int Disable_HighSide_Power_All(uint32_t timeout_ms);


/*ADC1 is used for Current Monitoring, 24V and 12V voltage monitoring, Thermistors*/
int Calibrate_ADC1(void);

/*DMA writes all ADC1 value to register. Start at beginning of main*/
int Start_ADC1_DMA(void);

/*Read the current from the IMON pin of the individual modules*/
int Read_HighSide_Module_Current_mA(HighSide_Module_t module, uint32_t *current_mA);

void Read_24V_Bus_Current_mA(uint32_t *current_mA);

/*Read 24V voltage potential divider*/
void Read_24V_Voltage_1DP(uint32_t *voltage_1DP);

/*Read 12V voltage potential divider*/
void Read_12V_Voltage_1DP(uint32_t *voltage_1DP);

/*Feat: Ensures 24V Bus is good before enabling 12V Bus
+returns PE_INVALID_PARAM if ADC_VAL is NULL
+returns PE_ERR_GEN if 24V bus is below minimum threshold
*/
int Enable_12V_Buck_Converter(void);

void Disable_12V_Buck_Converter(void);

int Read_Thermistor_Temperature_C(ADC_Peripheral_t thermistor, uint32_t *temperature_C);

void Shutdown_Protection(void);

/**
  * @brief  Reset the internal edge-detection state of Shutdown_Protection().
  *         Clears the static overvoltage_state and overcurrent_state[] variables
  *         so that Shutdown_Protection() will re-trigger on the next call if
  *         the fault condition is still present.
  *         Must be called when transitioning out of an error/recovery state.
  */
void Shutdown_Protection_ResetState(void);

/**
  * @brief  Handle CAN command to set Drive high-side power.
  * @param  id     CAN identifier of the incoming request (caller-provided, unused by this handler).
  * @param  params Pointer to parameter bytes; params[0] non-zero = enable, zero = disable.
  * @param  len    Number of parameter bytes; function returns immediately if len < 1.
  * @retval None
  *
  * @details
  * - Validates that at least one parameter byte is present.
  * - Calls Enable_HighSide_Power_Module(HS_MODULE_DRIVE, 500) when params[0] != 0,
  *   otherwise calls Disable_HighSide_Power_Module(HS_MODULE_DRIVE, 500).
  * - Sends a 1-byte acknowledgment on CAN ID 0x113 containing the applied state
  *   (1 = powered, 0 = removed) via FDCAN_SendFrame.
  * - Note: Enable/Disable helpers may block while waiting for power-good signals;
  *   FDCAN_SendFrame return value is not checked here.
  */
void CAN_Handler_Set_HS_Drive_Power(uint32_t id, uint8_t *params, uint8_t len);

/**
  * @brief  Handle CAN command to set Drive high-side power.
  * @param  id     CAN identifier of the incoming request (caller-provided, unused by this handler).
  * @param  params Pointer to parameter bytes; params[0] non-zero = enable, zero = disable.
  * @param  len    Number of parameter bytes; function returns immediately if len < 1.
  * @retval None
  *
  * @details
  * - Validates that at least one parameter byte is present.
  * - Calls Enable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 500) when params[0] != 0,
  *   otherwise calls Disable_HighSide_Power_Module(HS_MODULE_EXTRUDER, 500).
  * - Sends a 1-byte acknowledgment on CAN ID 0x113 containing the applied state
  *   (1 = powered, 0 = removed) via FDCAN_SendFrame.
  * - Note: Enable/Disable helpers may block while waiting for power-good signals;
  *   FDCAN_SendFrame return value is not checked here.
  */
void CAN_Handler_Set_HS_Extruder_Power(uint32_t id, uint8_t *params, uint8_t len);

/**
  * @brief  Handle CAN command to set Drive high-side power.
  * @param  id     CAN identifier of the incoming request (caller-provided, unused by this handler).
  * @param  params Pointer to parameter bytes; params[0] non-zero = enable, zero = disable.
  * @param  len    Number of parameter bytes; function returns immediately if len < 1.
  * @retval None
  *
  * @details
  * - Validates that at least one parameter byte is present.
  * - Calls Enable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 500) when params[0] != 0,
  *   otherwise calls Disable_HighSide_Power_Module(HS_MODULE_SCRUBBING, 500).
  * - Sends a 1-byte acknowledgment on CAN ID 0x113 containing the applied state
  *   (1 = powered, 0 = removed) via FDCAN_SendFrame.
  * - Note: Enable/Disable helpers may block while waiting for power-good signals;
  *   FDCAN_SendFrame return value is not checked here.
  */
void CAN_Handler_Set_HS_Scrubbing_Power(uint32_t id, uint8_t *params, uint8_t len);

/**
  * @brief  Pack 24V bus voltage (tenths of volts) into one CAN byte.
  * @param  out Pointer to destination buffer. Must be non-NULL and have at least 1 byte.
  * @param  out_size Size of the destination buffer in bytes (must be >= 1).
  *         The function writes a single byte to out[0].
  * @retval size_t Number of bytes written on success (1), or 0 on error
  *         (null pointer or insufficient buffer).
  *
  * @details
  * - Calls Read_24V_Voltage_1DP(&v_1DP) which returns the bus voltage in 0.1 V units
  *   (e.g., 240 => 24.0 V).
  * - The reported value is clamped to non-negative and logically capped at 300 (30.0 V).
  * - Because a single byte cannot represent values >255, values >255 are transmitted
  *   as 255 on the wire; receivers should interpret 255 as ">=255 (>=25.5 V)".
  */
size_t CAN_Packer_24V_Bus_2Byte(uint8_t *out, size_t out_size);


/**
  * @brief  Pack 12V bus voltage (tenths of volts) into one CAN byte.
  * @param  out Pointer to destination buffer. Must be non-NULL and have at least 1 byte.
  * @param  out_size Size of the destination buffer in bytes (must be >= 1).
  *         The function writes a single byte to out[0].
  * @retval size_t Number of bytes written on success (1), or 0 on error
  *         (null pointer or insufficient buffer).
  *
  * @details
  * - Calls Read_12V_Voltage_1DP(&v_1DP) which returns the bus voltage in 0.1 V units.
  * - The reported value is clamped to non-negative and logically capped at 300 (30.0 V).
  * - Values above 255 are transmitted as 255; receivers should interpret 255 as ">=255".
  */
size_t CAN_Packer_12V_Bus_1Byte(uint8_t *out, size_t out_size);


/**
  * @brief  Pack high-side module current into one CAN byte (0.1 A units).
  * @param  module HighSide_Module_t selecting which module to sample.
  * @param  out Pointer to destination buffer. Must be non-NULL and have at least 1 byte.
  * @param  out_size Size of the destination buffer in bytes (must be >= 1).
  *         The function writes a single byte to out[0].
  * @retval size_t Number of bytes written on success (1), or 0 on error
  *         (null pointer, insufficient buffer, or failure to read current).
  *
  * @details
  * - Reads current in milliamperes (mA) via Read_HighSide_Module_Current_mA().
  * - Converts mA -> 0.1 A units (tenths of amps) by dividing by 100 with rounding:
  *       current_1dp = (current_mA + 50) / 100
  *   (use truncation instead if you prefer exact floor).
  * - Logical cap at 300 (30.0 A). On-wire value is clamped to 255 if >255
  *   (255 represents ">=255" on the bus).
  */
size_t CAN_Packer_HighSide_Module_Current_1DP_1Byte(HighSide_Module_t module, uint8_t *out, size_t out_size);

/**
  * @brief  Pack 24V bus current into 2 CAN bytes (little-endian, 0.1A units).
  * @param  out      Destination buffer (at least 2 bytes).
  * @param  out_size Size of destination buffer.
  * @retval Bytes written (2) or 0 on error.
  *
  * Wire encoding: uint16_t little-endian in tenths of ampere (0.1A).
  *   e.g. 5.3A → 53,  12.7A → 127,  30.0A → 300
  * Receiver decodes: current_A = (uint16_t)(payload[1]<<8 | payload[0]) / 10.0
  */
size_t CAN_Packer_24V_Bus_Current_1DP_2Byte(uint8_t *out, size_t out_size);

/**
  * @brief  Pack thermistor temperature into one CAN byte (whole °C).
  * @param  thermistor ADC_Peripheral_t selecting which thermistor to sample
  *         (TEMP_PTC_1 … TEMP_PTC_6).
  * @param  out Pointer to destination buffer. Must be non-NULL and have at least 1 byte.
  * @param  out_size Size of the destination buffer in bytes (must be >= 1).
  *         The function writes a single byte to out[0].
  * @retval size_t Number of bytes written on success (1), or 0 on error
  *         (null pointer, insufficient buffer, or failure to read temperature).
  *
  * @details
  * - Reads temperature in whole °C via Read_Thermistor_Temperature_C().
  * - Offset encoding: transmitted value = temp_C + 40, so the byte represents
  *   -40 °C to +215 °C (0x00 = -40 °C, 0x28 = 0 °C, 0xFF = 215 °C).
  * - This allows sub-zero temperatures to be represented in a single unsigned byte.
  * - Values above 215 °C are clamped to 255 on-wire.
  */
size_t CAN_Packer_Thermistor_Temp_1Byte(ADC_Peripheral_t thermistor, uint8_t *out, size_t out_size);

/**
  * @brief  Pack overcurrent and overvoltage protection states into 1 CAN byte.
  *
  * Bit layout (MSB → LSB):
  *   [7:6]  overvoltage_state   (0=OK, 1=soft, 2=hard)
  *   [5:4]  overcurrent_state[2] — scrubbing  (0=OK, 1=overcurrent)
  *   [3:2]  overcurrent_state[1] — extruder   (0=OK, 1=overcurrent)
  *   [1:0]  overcurrent_state[0] — drive       (0=OK, 1=overcurrent)
  *
  * @param  out      Pointer to destination buffer (at least 1 byte).
  * @param  out_size Size of the destination buffer in bytes.
  * @retval Number of bytes written (1 on success, 0 on error).
  */
size_t CAN_Packer_Protection_State_1Byte(uint8_t *out, size_t out_size);

#endif /* POWER_ELECTRONIC_H_ */