/*
 * eeprom.c
 *
 *  Created on: 1 Dec 2025
 *      Author: jordan
 */

#include "math.h"
#include "eeprom_driver.h"
#include "main.h"
#include "string.h"
#include <stdbool.h>
#include "fdcan.h"
#include "CAN_Handler.h"
#define EEPROM_ADDR 0xA0 //eeprom address
#define PAGE_SIZE 64 //bytes per page
#define PAGE_NUM 512 //number in page

/*🟡Adapt*/
void LoadDefault(Config *config){
    config->magic = 0x3584;
    config->count = 200;
    config->mode = 0x01;
    config->pwm0 = 50;
    config->pwm1 = 30;
    config->hard_over_voltage = 26000;
    config->soft_over_voltage = 25500;
    config->under_voltage = 16000;
    config->over_current = 16000;
    config->fan_max_rpm = 5000;     /* default max fan speed */
}

/*🔴Preserve*/
bool checkcfg(Config *cfg){
	if(cfg->magic==0x3584)
		return true;
	else
		return false;
}

/*🔴Preserve*/
extern I2C_HandleTypeDef hi2c1;
//function to determine the remaining bytes
uint16_t bytes_to_write(uint16_t size, uint16_t offset){
	if((size+offset)<PAGE_SIZE)return size;
	else return PAGE_SIZE-offset;
}

static Config cached_cfg;
static bool cached_valid = false;
static void (*config_update_cb)(const Config* cfg) = NULL;

//write data to eeprom
// @page is numbber of start page, Range from 0 to PAGE_NUM-1
// @offset is start byte offset in the page. range from 0 to PAGE_SIZE-1
// @data is the pointer to data to write in bytes
// @size is the size of data
/*🔴Preserve*/
void EEPROM_Write (uint16_t page,uint16_t offset, uint8_t *data,uint16_t size){

	int paddrposition = log(PAGE_SIZE)/log(2); // calculate of bits where page addressing starts
	//calculate start page and end page
	uint16_t StartPage = page;
	uint16_t EndPage = page +((offset+size)/PAGE_SIZE);

	//number of page to be written

	uint16_t NumOfPages = (EndPage - StartPage)+1;
	uint16_t pos=0;

	//write data
	for(int i=0; i<NumOfPages; i++){
		// find address of memory location
		//add page address with byte address
		uint16_t MemAddress = StartPage<<paddrposition|offset;
		uint16_t bytesremaining = bytes_to_write(size, offset);

		if(HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, MemAddress, 2, &data[pos], bytesremaining, 1000) != HAL_OK){
            return;
        }
		StartPage+=1; //increment the page for new page address can be selected for future write
		offset=0; // for new page offset is 0
		size = size - bytesremaining; //recalculate the remaining size to write
		pos+=bytesremaining; // update the position for data to be written
		//HAL_Delay(5);
		for(int i=0; i <10; i++){
		if(HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDR, 1, 100)==HAL_OK)

			break;
		}
	}
}
//read data to eeprom

/*🔴Preserve*/
void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data,uint16_t size){
	int paddrposition = log(PAGE_SIZE)/log(2);
	uint16_t StartPage = page;
	uint16_t EndPage = page +((offset+size)/PAGE_SIZE);

	uint16_t NumOfPages = (EndPage-StartPage)+1;
	uint16_t pos = 0;

	for (int i=0;i<NumOfPages; i++){
		uint16_t MemAddress = StartPage<<paddrposition|offset;
		uint16_t BytesRemaining = bytes_to_write(size, offset);
		HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, MemAddress, 2, &data[pos],BytesRemaining, 1000);
		StartPage+=1;
		offset=0;
		size = size -BytesRemaining;
		pos+= BytesRemaining;
	}
}


//convert float number to bytes
uint32_t float2Bytes(float float_data){
	uint32_t converted_float_data;
	memcpy(&converted_float_data,&float_data,4);
	return converted_float_data;
}

//convert bytes to float
float bytes2Float(uint8_t buffer[4]){
	float f;
	uint32_t raw = ((uint32_t)buffer[3]<<24)|((uint32_t)buffer[2]<<16)|((uint32_t)buffer[1]<<8)|((uint32_t)buffer[0]);
	memcpy(&f,&raw, 4);
	return f;
}

//write float data to eeprom
void EEPROM_Write_Num(uint16_t page, uint16_t offset, float data){

	uint32_t raw =float2Bytes(data);
	uint8_t buffer[4];
	buffer[0] = (uint8_t)(raw&0xFF);
	buffer[1] = (uint8_t)((raw>>8)&0xFF);
	buffer[2] = (uint8_t)((raw>>16)&0xFF);
	buffer[3] = (uint8_t)((raw>>24)&0xFF);
	EEPROM_Write(page, offset, buffer, 4);

}

//read float data to eeprom
float EEPROM_Read_Num(uint16_t page,uint16_t offset){
	uint8_t buffer[4];
	EEPROM_Read(page, offset, buffer, 4);
	return(bytes2Float(buffer));
}

//erase the eeprom data
void EEPROM_pageErase(uint16_t page){
//calculate the memory address based on page number
int paddrposition = log(PAGE_SIZE)/log(2);
uint16_t MemAddress = page<<paddrposition;

//create a buffer to store reset values
uint8_t data[PAGE_SIZE];
memset(data,0xff,PAGE_SIZE);
//write to the dedicated page
HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, MemAddress, 2, data, PAGE_SIZE, 1000);
HAL_Delay(5);
for(int i=0; i <10; i++){
	if(HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDR, 1, 100)==HAL_OK)
		break;
	}
}



void EEPROM_Write_Config(uint16_t page, uint16_t offset, Config *cfg) {
    EEPROM_Write(page, offset, (uint8_t*)cfg, sizeof(Config));

    /* update cache and notify listeners (assume write succeeded; callers can verify if required) */
    cached_cfg = *cfg;
    cached_valid = true;
    if (config_update_cb) config_update_cb(&cached_cfg);
}

void EEPROM_Read_Config(uint16_t page, uint16_t offset, Config *cfg) {
    EEPROM_Read(page, offset, (uint8_t*)cfg, sizeof(Config));
}

/**
  * @brief  Validate individual config fields and clamp to defaults if out of range.
  * @param  cfg     Config to validate (modified in place)
  * @retval true    if one or more fields were repaired
  * @retval false   if all fields were already valid
  */
static bool EEPROM_SanitizeConfig(Config *cfg)
{
    Config defaults;
    LoadDefault(&defaults);
    bool repaired = false;

    /* hard_over_voltage: must be 10000–60000 mV */
    if (cfg->hard_over_voltage < 10000 || cfg->hard_over_voltage > 60000) {
        cfg->hard_over_voltage = defaults.hard_over_voltage;
        repaired = true;
    }

    /* soft_over_voltage: must be 10000–60000 mV AND below hard limit */
    if (cfg->soft_over_voltage < 10000 || cfg->soft_over_voltage > 60000
        || cfg->soft_over_voltage >= cfg->hard_over_voltage) {
        cfg->soft_over_voltage = defaults.soft_over_voltage;
        repaired = true;
    }

    /* under_voltage: must be 5000–30000 mV AND below soft_over_voltage */
    if (cfg->under_voltage < 5000 || cfg->under_voltage > 30000
        || cfg->under_voltage >= cfg->soft_over_voltage) {
        cfg->under_voltage = defaults.under_voltage;
        repaired = true;
    }

    /* over_current: must be 500–60000 mA */
    if (cfg->over_current < 500 || cfg->over_current > 60000) {
        cfg->over_current = defaults.over_current;
        repaired = true;
    }

    /* fan_max_rpm: must be 100–30000 */
    if (cfg->fan_max_rpm < 100 || cfg->fan_max_rpm > 30000) {
        cfg->fan_max_rpm = defaults.fan_max_rpm;
        repaired = true;
    }

    /* count: must be 1–10000 */
    if (cfg->count == 0 || cfg->count > 10000) {
        cfg->count = defaults.count;
        repaired = true;
    }

    /* pwm0/pwm1: must be 0–100 */
    if (cfg->pwm0 > 100) {
        cfg->pwm0 = defaults.pwm0;
        repaired = true;
    }
    if (cfg->pwm1 > 100) {
        cfg->pwm1 = defaults.pwm1;
        repaired = true;
    }

    return repaired;
}

/*🟡Adapt*/
void EEPROM_Init(void)
{
    const uint16_t cfg_page = 0;
    const uint16_t cfg_offset = 0;
    Config cfg;
    /* Read existing config from EEPROM (page 0 offset 0) */
    EEPROM_Read_Config(cfg_page, cfg_offset, &cfg);
    /* If config magic is invalid, write defaults and verify */
    if (!checkcfg(&cfg)) {
        LoadDefault(&cfg);
        EEPROM_Write_Config(cfg_page, cfg_offset, &cfg);
        Config verify;
        EEPROM_Read_Config(cfg_page, cfg_offset, &verify);
        if (checkcfg(&verify)) {
            cached_cfg = verify;
            cached_valid = true;
            if (config_update_cb) config_update_cb(&cached_cfg);
            return;
        } else {
            cached_valid = false;
            return;
        }
    }

    /* Magic is valid — but individual fields may be corrupt or uninitialized.
       Sanitize each field against allowed ranges; rewrite if any were repaired. */
    if (EEPROM_SanitizeConfig(&cfg)) {
        EEPROM_Write_Config(cfg_page, cfg_offset, &cfg);
    }

    /* already valid: store in cache */
    cached_cfg = cfg;
    cached_valid = true;
    if (config_update_cb) config_update_cb(&cached_cfg);
}
/* Return pointer to cached config or NULL if not initialised */
const Config* EEPROM_GetCachedConfig(void)
{
    return cached_valid ? &cached_cfg : NULL;
}



void CAN_Handler_EEPROM_Write_Config(uint32_t id, uint8_t *params, uint8_t len)
{
    const uint16_t cfg_page = 0;
    const uint16_t cfg_offset = 0;

    if (params == NULL || len < 3) {
        return;
    }

    uint8_t setting = params[0];
    uint16_t value = (uint16_t)params[1] | ((uint16_t)params[2] << 8);

    /* Basic sanity: reject 0 */
    if (value == 0) {
        return;
    }

    Config cfg;
    EEPROM_Read_Config(cfg_page, cfg_offset, &cfg);
    if (!checkcfg(&cfg)) {
        LoadDefault(&cfg);
    }

    switch (setting) {
        case EEPROM_SETTING_HARD_OV:
            if (value < 10000 || value > 60000) return;
            cfg.hard_over_voltage = value;
            break;
        case EEPROM_SETTING_SOFT_OV:
            if (value < 10000 || value > 60000) return;
            /* soft must be below hard */
            if (value >= cfg.hard_over_voltage) return;
            cfg.soft_over_voltage = value;
            break;
        case EEPROM_SETTING_UNDER_V:
            if (value < 5000 || value > 30000) return;
            /* under must be below soft */
            if (value >= cfg.soft_over_voltage) return;
            cfg.under_voltage = value;
            break;
        case EEPROM_SETTING_OVER_CURR:
            if (value < 500 || value > 60000) return;
            cfg.over_current = value;
            break;
        case EEPROM_SETTING_FAN_MAX_RPM:
            if (value < 100 || value > 30000) return;
            cfg.fan_max_rpm = value;
            break;
        default:
            return; /* unknown setting */
    }

    EEPROM_Write_Config(cfg_page, cfg_offset, &cfg);

    /* Send ACK on 0x604: echo back the setting + written value */
    uint8_t ack[3] = {
        setting,
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };
    FDCAN_SendFrame(0x604, ack, 3);
}


void CAN_Handler_EEPROM_Read_Config(uint32_t id, uint8_t *params, uint8_t len)
{
    const Config *cfgp = EEPROM_GetCachedConfig();
    Config local_cfg;

    if (cfgp == NULL) {
        EEPROM_Read_Config(0, 0, &local_cfg);
        if (checkcfg(&local_cfg)) {
            cfgp = &local_cfg;
        }
    }

    /* Frame 1: 0x604 — hard OV, soft OV, over current, fan max RPM */
    {
        uint8_t payload[8] = {0};

        if (cfgp != NULL) {
            payload[0] = (uint8_t)(cfgp->hard_over_voltage & 0xFF);
            payload[1] = (uint8_t)((cfgp->hard_over_voltage >> 8) & 0xFF);
            payload[2] = (uint8_t)(cfgp->soft_over_voltage & 0xFF);
            payload[3] = (uint8_t)((cfgp->soft_over_voltage >> 8) & 0xFF);
            payload[4] = (uint8_t)(cfgp->over_current & 0xFF);
            payload[5] = (uint8_t)((cfgp->over_current >> 8) & 0xFF);
            payload[6] = (uint8_t)(cfgp->fan_max_rpm & 0xFF);
            payload[7] = (uint8_t)((cfgp->fan_max_rpm >> 8) & 0xFF);
        }

        FDCAN_SendFrame(0x604, payload, 8);
    }

    /* Frame 2: 0x605 — under voltage */
    {
        uint8_t payload[2] = {0};

        if (cfgp != NULL) {
            payload[0] = (uint8_t)(cfgp->under_voltage & 0xFF);
            payload[1] = (uint8_t)((cfgp->under_voltage >> 8) & 0xFF);
        }

        FDCAN_SendFrame(0x605, payload, 2);
    }
}

