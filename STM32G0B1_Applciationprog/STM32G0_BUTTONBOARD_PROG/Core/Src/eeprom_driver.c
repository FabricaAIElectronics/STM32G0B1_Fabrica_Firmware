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
#define EEPROM_ADDR 0xA0 //eeprom address
#define PAGE_SIZE 64 //bytes per page
#define PAGE_NUM 512 //number in page

void LoadDefault(Config *config){
	/* Dark and under the direct/J1 path. Coming up with the STM32 owning the
	 * LED nets would mean a board with no host on the bus sits there driving a
	 * pattern nobody asked for; coming up dark and passive is the state an
	 * operator can reason about. */
	config->magic                = EEPROM_CFG_MAGIC;
	config->default_led_mask     = 0x00;
	config->default_led_int_mask = 0x00;
	config->default_led_source   = 0;
	config->flags                = 0x00;
	config->reserved[0]          = 0;
	config->reserved[1]          = 0;
}

bool checkcfg(Config *cfg){
	if(cfg->magic == EEPROM_CFG_MAGIC)
		return true;
	else
		return false;
}
extern I2C_HandleTypeDef hi2c3;
//function to determine the remaining bytes
uint16_t bytes_to_write(uint16_t size, uint16_t offset){
	if((size+offset)<PAGE_SIZE)return size;
	else return PAGE_SIZE-offset;
}

//write data to eeprom
// @page is numbber of start page, Range from 0 to PAGE_NUM-1
// @offset is start byte offset in the page. range from 0 to PAGE_SIZE-1
// @data is the pointer to data to write in bytes
// @size is the size of data
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

		HAL_I2C_Mem_Write(&hi2c3, EEPROM_ADDR, MemAddress, 2, &data[pos], bytesremaining, 1000);
		StartPage+=1; //increment the page for new page address can be selected for future write
		offset=0; // for new page offset is 0
		size = size - bytesremaining; //recalculate the remaining size to write
		pos+=bytesremaining; // update the position for data to be written
	}
}
//read data to eeprom

void EEPROM_Read (uint16_t page, uint16_t offset, uint8_t *data,uint16_t size){
	int paddrposition = log(PAGE_SIZE)/log(2);
	uint16_t StartPage = page;
	uint16_t EndPage = page +((offset+size)/PAGE_SIZE);

	uint16_t NumOfPages = (EndPage-StartPage)+1;
	uint16_t pos = 0;

	for (int i=0;i<NumOfPages; i++){
		uint16_t MemAddress = StartPage<<paddrposition|offset;
		uint16_t BytesRemaining = bytes_to_write(size, offset);
		HAL_I2C_Mem_Read(&hi2c3, EEPROM_ADDR, MemAddress, 2, &data[pos],BytesRemaining, 1000);
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
HAL_I2C_Mem_Write(&hi2c3, EEPROM_ADDR, MemAddress, 2, data, PAGE_SIZE, 1000);
HAL_Delay(5);
for(int i=0; i <10; i++){
	if(HAL_I2C_IsDeviceReady(&hi2c3, EEPROM_ADDR, 1, 100)==HAL_OK)
		break;
	}
}



bool EEPROM_Write_Config(const Config *config)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c3, EEPROM_ADDR, 0U, 2,
                                             (uint8_t *)config, sizeof(Config), 100U);
    if (st == HAL_OK) {
        /* AT24C256 internal write cycle is ~5 ms: poll with HAL's own
         * trials loop instead of the old 10 fast NACK probes. */
        st = HAL_I2C_IsDeviceReady(&hi2c3, EEPROM_ADDR, 300U, 10U);
    }
    return st == HAL_OK;
}

bool EEPROM_Read_Config(Config *config)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c3, EEPROM_ADDR, 0U, 2,
                                            (uint8_t *)config, sizeof(Config), 100U);
    return st == HAL_OK;
}
