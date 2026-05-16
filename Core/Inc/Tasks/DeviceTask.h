#ifndef DEVICE_TASK_H
#define DEVICE_TASK_H

#include "taskutils.h"

extern osThreadId_t DeviceTaskHandle;

void DeviceTaskInit( void );
void DeviceTaskLoop( void );

#endif // DEVICE_TASK_H