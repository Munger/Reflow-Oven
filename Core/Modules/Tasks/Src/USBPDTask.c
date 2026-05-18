/// @file USBPDTask.c
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

#include "Features.h"

#if FEATURE_USB_PD

#include "USBPDTask.h"
#include "SystemStatusFlags.h"
#include "USBPowerDelivery.h"

enum { kUSBPDTickMs = 10 };

void USBPDTaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ),
                      osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

void USBPDTaskLoop( void ) {
    osDelay( kUSBPDTickMs );
    USBPDProcess();
}

#endif // FEATURE_USB_PD
