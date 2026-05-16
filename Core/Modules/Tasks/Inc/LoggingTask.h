/// @file LoggingTask.h
///
/// @brief Logging task — telemetry and event log persistence.
///
/// The Logging task is responsible for capturing sensor data and system events
/// to non-volatile storage. The loop body is currently a placeholder pending
/// flash / file manager implementation.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef LOGGING_TASK_H
#define LOGGING_TASK_H

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the Logging task.
extern osThreadId_t LoggingTaskHandle;

/// @brief Initialise the Logging task — waits for system initialisation signal.
///
/// Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning.
/// Called once by app_freertos.c at startup.
void LoggingTaskInit( void );

/// @brief Main execution body of the Logging task loop.
///
/// Currently a placeholder — implementation pending file manager driver.
void LoggingTaskLoop( void );

#endif // LOGGING_TASK_H
