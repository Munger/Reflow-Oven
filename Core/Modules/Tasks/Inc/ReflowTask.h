/// @file ReflowTask.h
///
/// @brief Reflow task — profile execution and thermal control loop.
///
/// The Reflow task runs the active reflow profile, driving the heater zones
/// through preheat, soak, reflow, and cool stages. The loop body is currently
/// a placeholder pending the reflow profile and OvenController integration.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef REFLOW_TASK_H
#define REFLOW_TASK_H

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the Reflow task.
extern osThreadId_t ReflowTaskHandle;

/// @brief Initialise the Reflow task — waits for system initialisation signal.
///
/// Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning.
/// Called once by app_freertos.c at startup.
void ReflowTaskInit( void );

/// @brief Main execution body of the Reflow task loop.
///
/// Currently a placeholder — implementation pending reflow profile driver.
void ReflowTaskLoop( void );

#endif // REFLOW_TASK_H
