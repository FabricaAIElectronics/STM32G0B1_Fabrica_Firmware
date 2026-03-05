/*
 * canopen_bridge.h
 *
 * Bridge between STM32 FDCAN HAL and the CANopen library.
 * All device parameters are exposed via CANopen SDO object dictionary.
 */

#ifndef INC_CANOPEN_BRIDGE_H_
#define INC_CANOPEN_BRIDGE_H_

#include "canopen.h"
#include "stm32g0xx_hal.h"

/* CANopen node ID for this device */
#define CANOPEN_NODE_ID  0x10

/* Forward declaration — avoids circular include with applogic.h */
typedef struct AppStateMachine_ AppStateMachine;

/*
 * @brief Initialize CANopen ring buffers, object dictionary, and NMT boot-up.
 * @param sm Pointer to app state machine (used by SDO read callbacks for live data)
 */
void CanOpenBridge_Init(AppStateMachine *sm);

/*
 * @brief Process all queued RX frames (NMT + SDO) from the main loop.
 */
void CanOpenBridge_ProcessRx(void);

/*
 * @brief Transmit all queued TX frames (SDO responses, heartbeats) via CAN_Send.
 */
void CanOpenBridge_SendPending(void);

/*
 * @brief Queue a heartbeat frame based on current NMT state. Call periodically.
 */
void CanOpenBridge_SendHeartbeat(void);

/*
 * @brief ISR entry point — called from HAL_FDCAN_RxFifo0Callback (SDO/PDO frames).
 */
void CanOpenBridge_OnRxIsr(FDCAN_RxHeaderTypeDef *header, uint8_t *data);

/*
 * @brief ISR entry point — called from HAL_FDCAN_RxFifo1Callback (NMT frames).
 */
void CanOpenBridge_OnNmtRxIsr(FDCAN_RxHeaderTypeDef *header, uint8_t *data);

#endif /* INC_CANOPEN_BRIDGE_H_ */
