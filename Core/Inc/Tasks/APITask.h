/// @file APITask.h
///
/// @brief USB REST API task — public interface for the API task loop.
///
/// The API task processes incoming USB CDC requests, dispatches them to route
/// handlers, serialises responses, and drives the USB transmission pipeline.
/// USBTxDoneHandler() is called directly from the USB ISR.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef API_TASK_H
#define API_TASK_H

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the API task.
extern osThreadId_t APITaskHandle;

/// @brief Initialise the API core and buffer stream subsystems.
///
/// Waits for FlagSystemInitialised in SystemStatusFlagsHandle before proceeding.
/// Called once by app_freertos.c at startup.
void APITaskInit( void );

/// @brief Main execution body of the API task loop.
///
/// Blocks on xTaskNotifyWait(), processes pending requests via the route table,
/// queues serialised responses, and drains the USB transmit queue.
/// Called repeatedly by app_freertos.c in the task's infinite loop.
void APITaskLoop( void );

/// @brief USB transmission complete callback — advances the transmit pipeline.
///
/// Called directly from the USB CDC ISR (usbd_cdc_if.c). Releases the completed
/// buffer, starts the next link in the chain if present, or wakes the API task
/// via task notification to re-check the output queue.
///
/// @warning ISR context. Uses FromISR notification variants only.
void USBTxDoneHandler( void );

#endif // API_TASK_H
