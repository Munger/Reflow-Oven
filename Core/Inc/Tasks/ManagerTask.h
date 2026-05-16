/// @file ManagerTask.h
///
/// @brief Manager task — system supervisor and fault monitor.
///
/// ManagerTask initialises all hardware driver modules, waits for DEVICE_ALL_READY,
/// then enables interrupts and signals system initialisation. After startup, the loop
/// blocks on any fault in FaultFlagsHandle for supervisor reaction.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef MANAGER_TASK_H
#define MANAGER_TASK_H

#include "TaskUtils.h"
#include "SystemStatusFlags.h"

/// @brief FreeRTOS thread handle for the Manager task.
extern osThreadId_t ManagerTaskHandle;

/// @brief Initialise all hardware driver modules and signal system readiness.
///
/// Calls XxxInitModule() for every driver in dependency order. Waits for
/// DEVICE_ALL_READY before enabling interrupts and setting FlagSystemInitialised.
/// Called once by app_freertos.c at startup.
void ManagerTaskInit( void );

/// @brief Main execution body of the Manager task loop — fault supervisor.
///
/// Blocks indefinitely on FaultFlagsHandle (any fault bit). The notification
/// value is captured but not yet acted upon; fault handling policy is TBD.
/// Called repeatedly by app_freertos.c in the task's infinite loop.
void ManagerTaskLoop( void );

#endif // MANAGER_TASK_H
