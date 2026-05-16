#ifndef MANAGER_TASK_H
#define MANAGER_TASK_H

#include "taskutils.h"
#include "SystemStatusFlags.h"

extern osThreadId_t ManagerTaskHandle;

void ManagerTaskInit( void );
void ManagerTaskLoop( void );

#endif // MANAGER_TASK_H