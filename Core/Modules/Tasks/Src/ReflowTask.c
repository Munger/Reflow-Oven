/// @file ReflowTask.c
///
/// @brief Reflow task implementation — executes the active reflow profile.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "ReflowTask.h"
#include "SystemStatusFlags.h"
#include "Reflow.h"

void ReflowTaskInit( void ) {
    ReflowInitModule();
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

void ReflowTaskLoop( void ) {
    ReflowProcess();
}
