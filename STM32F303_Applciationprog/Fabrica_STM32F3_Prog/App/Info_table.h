/*
 * Info_table.h
 *
 *  Created on: Nov 6, 2025
 *      Author: jordan
 */

#ifndef INFO_TABLE_H_
#define INFO_TABLE_H_
#include <stdint.h>

typedef struct t_firmware_info_table
{
  uint32_t tableId;
  uint32_t productId;
  uint32_t firmwareVersion;
} tFirmwareInfoTable;

/* Extern declaration only */
extern const tFirmwareInfoTable firmwareInfoTable;

#endif /* INFO_TABLE_H_ */
