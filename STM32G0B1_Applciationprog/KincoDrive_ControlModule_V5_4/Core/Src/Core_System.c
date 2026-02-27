#include "Power_Electronic.h"
#include "eeprom_driver.h"
#include "ESTOP.h"
#include "Core_Systems.h"
#include "error_manager.h"

void CoreSystem_PeriodicTasks(void)
{
    /* Check for overvoltage/overcurrent conditions and shutdown if needed */
    Shutdown_Protection();
    ESTOP_State_Machine();

    /* Enforce error state: keep locked-out modules disabled */
    Error_Manager_EnforceState();

    
}

void CoreSystem_Normal(void)
{
    CoreSystem_PeriodicTasks();
}

void CoreSystem_Recovery(void)
{

    /* If in recovery, validate readings before allowing NORMAL */
    Error_Manager_AttemptRecovery();
}

void CoreSystem_Error(void)
{
    /* In error state, just keep enforcing the lockout and wait for reset */
    CoreSystem_PeriodicTasks();
}

void CoreSystem_TOP(void)
{
    SystemState_t st = Error_Manager_GetState();
    if (st == STATE_ERROR) {
        CoreSystem_Error();
    } else if (st == STATE_RECOVERY) {
        CoreSystem_Recovery();
    } else if (st == STATE_WARNING) {
        CoreSystem_Normal(); /* warnings don't block operation */
    } else {
        CoreSystem_Normal();
    }
}