/// @file APITask.c
///
/// @brief USB REST API task — notification wait and DriverRegistry iteration.
///
/// Blocks on task notifications from the USB ISR and the API core. Runs every
/// TaskOwnerAPI process in the DriverRegistry — currently USBCDCProcess() which
/// handles request dispatch and the CDC transmit pipeline.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "SystemStatusFlags.h"
#include "DriverRegistry.h"
#include "APITask.h"

/// @brief Wait for the system to be fully initialised before the task loop starts.
///
/// APICoreInit() and APIStreamInit() are called from USBCDCInitModule() during
/// ManagerTaskInit. Called once by app_freertos.c at startup.
void APITaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

/// @brief API task main loop body — iterate all TaskOwnerAPI processes from the
///        DriverRegistry.
///
/// Blocks on xTaskNotifyWait() then runs every registered TaskOwnerAPI process.
/// Currently the only entry is USBCDCProcess() which handles request dispatch
/// and the CDC transmit chain.
///
/// Called repeatedly by app_freertos.c in the task's infinite loop.
void APITaskLoop( void ) {
    xTaskNotifyWait( 0, 0xFFFFFFFF, NULL, portMAX_DELAY );

    int count = 0;
    DriverEntryPtr table = DriverTable( &count );
    for ( int i = 0; i < count; i++ ) {
        if ( table[ i ].task == TaskOwnerAPI && table[ i ].process ) {
            table[ i ].process();
        }
    }
}
