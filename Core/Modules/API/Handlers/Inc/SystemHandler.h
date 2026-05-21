/// @file SystemHandler.h
///
/// @brief Handler declarations for the /system endpoint group.
///
/// Covers status, power, clock, debug mode, graceful stop, and reset.
/// All /system handlers dispatch on reqCode only — no sscanf extraction
/// needed as none of these routes carry typed wildcards.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef SYSTEMHANDLER_H
#define SYSTEMHANDLER_H

#include "APITypes.h"

/// @brief Return system status (flags, uptime, firmware version).
APIPBPtr HandlerSystemStatus( APIPBPtr pb );

/// @brief Return API pool utilisation statistics.
APIPBPtr HandlerSystemStats( APIPBPtr pb );

/// @brief Return USB PD / power rail status.
APIPBPtr HandlerSystemPower( APIPBPtr pb );

/// @brief Gracefully stop all hot-side drivers and isolate power.
APIPBPtr HandlerSystemStop( APIPBPtr pb );

/// @brief Return the current RTC date and time.
APIPBPtr HandlerClockGet( APIPBPtr pb );

/// @brief Set the RTC date and time.
APIPBPtr HandlerClockPut( APIPBPtr pb );

/// @brief Enable or disable debug mode (extra flag detail in device GET responses).
APIPBPtr HandlerSystemDebug( APIPBPtr pb );

/// @brief Software reset of the MCU.
APIPBPtr HandlerSystemReset( APIPBPtr pb );

#endif // SYSTEMHANDLER_H
