/// @file USBPDTask.c
///
/// @brief USB Power Delivery task — independent process loop.
///
/// Runs USBPDProcess() on a fixed 10 ms tick, independent of DeviceTask and
/// all other peripherals. This guarantees that role detection, fault autopsy,
/// voltage requests, and telemetry refresh are serviced regardless of system
/// load on other tasks.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "USBPDTask.h"
#include "SystemStatusFlags.h"
#include "USBPowerDelivery.h"

enum { kUSBPDTickMs = 10 };

/// @brief Waits for system initialisation before starting the PD process loop.
void USBPDTaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ),
                      osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

/// @brief Drive the PD policy engine at a fixed 10 ms tick.
void USBPDTaskLoop( void ) {
    osDelay( kUSBPDTickMs );
    USBPDProcess();
}
