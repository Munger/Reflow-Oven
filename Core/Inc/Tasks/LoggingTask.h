#ifndef LOGGING_TASK_H
#define LOGGING_TASK_H

#include "taskutils.h"

extern osThreadId_t LoggingTaskHandle;

void LoggingTaskInit( void );
void LoggingTaskLoop( void );

#endif // LOGGING_TASK_H