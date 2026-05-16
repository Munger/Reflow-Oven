/// @file DeviceTask.h
///
/// @brief Device task — periodic driver process loop.
///
/// The Device task calls the XxxProcess() function of each hardware driver
/// on every tick. All hardware I/O, flag updates, and fault propagation for
/// each peripheral module occur inside its corresponding Process() function.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef DEVICE_TASK_H
#define DEVICE_TASK_H

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the Device task.
extern osThreadId_t DeviceTaskHandle;

/// @brief Initialise the Device task — waits for system initialisation signal.
///
/// Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning.
/// Called once by app_freertos.c at startup.
void DeviceTaskInit( void );

/// @brief Main execution body of the Device task loop.
///
/// Calls the Process() function for every driver module in sequence.
/// Called repeatedly by app_freertos.c in the task's infinite loop.
void DeviceTaskLoop( void );

#endif // DEVICE_TASK_H
