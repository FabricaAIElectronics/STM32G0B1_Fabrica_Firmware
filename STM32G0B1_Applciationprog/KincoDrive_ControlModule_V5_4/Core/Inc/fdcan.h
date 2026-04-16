/**
  ******************************************************************************
  * @file    fdcan.h
  * @brief   FDCAN peripheral handle and TX helper.
  ******************************************************************************
  */

#ifndef __FDCAN_H__
#define __FDCAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;

void MX_FDCAN1_Init(void);

/**
 * @brief  Transmit one Classic CAN frame using a 29-bit extended identifier.
 *
 * @param  ext_id  Full 29-bit extended CAN ID.
 * @param  data    Pointer to payload data (may be NULL if len == 0).
 * @param  len     Payload length in bytes (clamped to 8).
 * @retval true on success, false on TX queue full or HAL error.
 */
bool FDCAN_SendFrame(uint32_t ext_id, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __FDCAN_H__ */
