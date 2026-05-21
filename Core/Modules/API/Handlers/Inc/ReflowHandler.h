/// @file ReflowHandler.h
///
/// @brief Handler declarations for the /reflow endpoint group.
///
/// The reflow state machine is a singleton process that orchestrates
/// profile execution by commanding OvenController driver instances.
/// These handlers do not touch hardware directly.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef REFLOWHANDLER_H
#define REFLOWHANDLER_H

#include "APITypes.h"

/// @brief Return the current reflow cycle state, active profile, and stage.
APIPBPtr HandlerReflowStatus( APIPBPtr pb );

/// @brief Start a named reflow profile.
APIPBPtr HandlerReflowRun( APIPBPtr pb );

/// @brief Gracefully stop the active cycle.
APIPBPtr HandlerReflowStop( APIPBPtr pb );

#endif // REFLOWHANDLER_H
