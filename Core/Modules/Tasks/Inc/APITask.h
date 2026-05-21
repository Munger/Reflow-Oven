/// @file APITask.h
///
/// @brief USB REST API task — public interface for the API task loop.
///
/// The API task blocks on task notifications and iterates the DriverRegistry
/// for TaskOwnerAPI processes — currently USBCDCProcess which handles both
/// request dispatch and the CDC transmit pipeline.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef API_TASK_H
#define API_TASK_H

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the API task.
extern osThreadId_t APITaskHandle;

/// @brief Wait for FlagSystemInitialised before the task loop starts.
/// Called once by app_freertos.c at startup.
void APITaskInit( void );

/// @brief Block on notification, then run every TaskOwnerAPI process registered
///        in the DriverRegistry. Called repeatedly by app_freertos.c.
void APITaskLoop( void );

#endif // API_TASK_H
