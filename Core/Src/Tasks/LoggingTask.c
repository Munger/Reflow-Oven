/// @file LoggingTask.c
///
/// @brief Logging task implementation — placeholder pending file manager driver.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "LoggingTask.h"
#include "SystemStatusFlags.h"

/// @brief Initialise the Logging task — waits for system initialisation before proceeding.
void LoggingTaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

/// @brief Logging task loop body — currently empty pending file manager implementation.
void LoggingTaskLoop( void ) {
}
