/*
 * display_scheduler.c
 * 
 * Simple Scheduler for OLED Display Updates
 */

#include "display_scheduler.h"
#include "ssd1306.h"

/* ─── Private State ─── */
static uint32_t s_last_update_time = 0;
static uint32_t s_update_count = 0;
static bool s_update_needed = false;

/* ─── Initialize Scheduler ─── */
void DisplayScheduler_Init(void)
{
    s_last_update_time = HAL_GetTick();
    s_update_count = 0;
    s_update_needed = true;  /* Force first update */
}

/* ─── Check if Update is Needed ─── */
bool DisplayScheduler_ShouldUpdate(void)
{
    uint32_t now = HAL_GetTick();
    
    if ((now - s_last_update_time) >= DISPLAY_UPDATE_INTERVAL_MS)
    {
        s_update_needed = true;
        s_last_update_time = now;
    }
    
    return s_update_needed;
}

/* ─── Mark Update as Complete ─── */
void DisplayScheduler_MarkUpdated(void)
{
    s_update_needed = false;
    s_update_count++;
}

/* ─── Get Update Count ─── */
uint32_t DisplayScheduler_GetUpdateCount(void)
{
    return s_update_count;
}

/* ─── Scheduler Task (Call in main loop) ─── */
void DisplayScheduler_Task(void)
{
    if (DisplayScheduler_ShouldUpdate())
    {
        /* Call user-defined content update function */
        DisplayContent_Update();
        
        /* Mark as updated */
        DisplayScheduler_MarkUpdated();
    }
}
