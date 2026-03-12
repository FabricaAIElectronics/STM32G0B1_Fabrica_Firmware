#ifndef ESTOP_H_
#define ESTOP_H_

#include <stdint.h>
#include "main.h"

#define ESTOP_OK          0
#define ESTOP_TRIGGERED   1
#define ESTOP_FAULT      -1

void ESTOP_Init(void);

int ESTOP_Check(void);

void ESTOP_State_Machine(void);

#endif
