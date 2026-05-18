/// @file USBPDTask.h
///
/// @brief USB Power Delivery task — independent 10 ms process loop.
///
/// Runs USBPDProcess() on a dedicated tick, isolated from DeviceTask so that
/// role detection, fault autopsy, voltage changes, and telemetry refresh are
/// never delayed by sensor polling or other peripheral I/O.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef USBPD_TASK_H
#define USBPD_TASK_H

#include "Features.h"

#if FEATURE_USB_PD

#include "TaskUtils.h"

/// @brief FreeRTOS thread handle for the USBPD task.
extern osThreadId_t USBPDTaskHandle;

/// @brief Waits for FlagSystemInitialised before allowing the process loop to run.
void USBPDTaskInit( void );

/// @brief Call USBPDProcess() every 10 ms.
void USBPDTaskLoop( void );

#endif // FEATURE_USB_PD

#endif // USBPD_TASK_H
