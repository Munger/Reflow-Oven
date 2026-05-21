/// @file DeviceTask.c
///
/// @brief Device task — periodic driver process loop implementation.
///
/// DeviceTaskLoop() iterates the DriverRegistry for entries owned by TaskOwnerDevice
/// and calls each entry's process() function in table order. The ordering is
/// chosen to minimise inter-module latency: power management and MCU first,
/// then sensors, then actuators, then the control loop.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"
#include "DeviceTask.h"
#include "DriverRegistry.h"
#include "SystemStatusFlags.h"
#include "I2CManager.h"
#if FEATURE_BOARD_FAN
#include "DCFan.h"
#endif
#if FEATURE_THERMISTOR_HEATSINK
#include "ThermistorI2C.h"
#endif

/// @brief Initialise the Device task — waits for system initialisation then opens I2C peripherals.
///
/// Blocks on FlagSystemInitialised to ensure all modules are initialised. Then opens
/// any I2C-connected peripherals that this task owns, supplying the bus reference they
/// need. Hardware errors during Open() set fault flags rather than blocking startup.
void DeviceTaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );

#if FEATURE_BOARD_FAN || FEATURE_THERMISTOR_HEATSINK
    I2CRef i2c = I2COpen( I2CBus1 );
#endif // FEATURE_BOARD_FAN || FEATURE_THERMISTOR_HEATSINK

#if FEATURE_BOARD_FAN
    DCFanOpen( BoardCoolingFan, i2c );
#endif // FEATURE_BOARD_FAN

#if FEATURE_THERMISTOR_HEATSINK
    TMI2COpen( ThermistorI2C1, i2c, NULL );
#endif // FEATURE_THERMISTOR_HEATSINK
}

/// @brief Call the Process() function for every DeviceTask-owned driver in table order.
///
/// Each Process() function performs all hardware I/O, updates cached state, and
/// sets/clears status and fault flags for its respective module. No hardware access
/// occurs outside of these calls in this task.
void DeviceTaskLoop( void ) {
    int count = 0;
    DriverEntryPtr table = DriverTable( &count );
    for ( int i = 0; i < count; i++ ) {
        if ( table[ i ].task == TaskOwnerDevice && table[ i ].process ) {
            table[ i ].process();
        }
    }
}
