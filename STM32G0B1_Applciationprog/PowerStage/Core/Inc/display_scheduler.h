/*
 * display_scheduler.h
 * 
 * Simple Scheduler for OLED Display Updates
 * Manages periodic screen refreshes without blocking
 */

#ifndef DISPLAY_SCHEDULER_H
#define DISPLAY_SCHEDULER_H

#include "stm32g0xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ─── Configuration ─── */
#define DISPLAY_UPDATE_INTERVAL_MS  500   /* Update every 500ms */

/* ─── Scheduler API ─── */
void DisplayScheduler_Init(void);
void DisplayScheduler_Task(void);
bool DisplayScheduler_ShouldUpdate(void);
void DisplayScheduler_MarkUpdated(void);
uint32_t DisplayScheduler_GetUpdateCount(void);

/* ─── Optional: Content Update Callback ─── */
/* Implement this function in your app.c to update display content */
/* It will be called every DISPLAY_UPDATE_INTERVAL_MS */
void DisplayContent_Update(void);

#endif /* DISPLAY_SCHEDULER_H */