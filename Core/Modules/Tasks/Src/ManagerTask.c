/// @file ManagerTask.c
///
/// @brief Manager task — driver initialisation sequencer and fault supervisor.
///
/// Iterates the DriverRegistry table in order, calling each entry's init()
/// function. Bus managers (SPI, I2C) are first in the table so their semaphores
/// exist before any driver that calls SPIOpen() or I2COpen() internally.
///
/// Device Open() calls are the responsibility of each consuming module:
/// OvenController opens its own thermocouples and thermistor, Reflow opens
/// OvenController and ACFan, DeviceTask opens remaining peripherals. ManagerTask
/// waits for DEVICE_ALL_READY once those tasks have run their open, then enables
/// GPIO interrupts and broadcasts FlagSystemInitialised.
///
/// After startup the loop blocks on any FaultFlagsHandle bit for supervisory response.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"
#include "ManagerTask.h"
#include "DriverRegistry.h"

/// @brief Initialise all driver modules in table order, then signal system readiness.
///
/// Iterates the DriverRegistry in declaration order (bus managers first, then
/// remaining drivers). Each InitModule() sets its own ready bit in
/// DeviceStatusFlagsHandle on completion, satisfying DEVICE_ALL_READY.
///
/// Phase 3 — wait for DEVICE_ALL_READY, enable GPIO interrupts, broadcast FlagSystemInitialised.
void ManagerTaskInit( void ) {
    int count = 0;
    DriverEntryPtr table = DriverTable( &count );
    for ( int i = 0; i < count; i++ ) {
        if ( table[ i ].init ) {
            table[ i ].init();
        }
    }

    // Wait for all devices, enable interrupts, signal init complete
    osEventFlagsWait( DeviceStatusFlagsHandle, DEVICE_ALL_READY, osFlagsWaitAll | osFlagsNoClear, osWaitForever );
    osEventFlagsSet( SystemStatusFlagsHandle, BIT( FlagInterruptsEnabled ) );
    osEventFlagsSet( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ) );
}

/// @brief Supervisor loop — blocks on any active fault and reacts accordingly.
///
/// Currently captures the fault bitmask but does not implement a reaction policy.
/// Fault handling (safe-state enforcement, buzzer alert, etc.) is TBD.
void ManagerTaskLoop( void ) {
    uint32_t faults = osEventFlagsWait( FaultFlagsHandle, FAULT_ANY, osFlagsWaitAny | osFlagsNoClear, osWaitForever );
    (void)faults;
}
