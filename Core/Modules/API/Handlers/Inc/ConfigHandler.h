/// @file ConfigHandler.h
///
/// @brief Handler declarations for the /config endpoint group.
///
/// Exposes a JSON key-value store for device configuration.
/// GET returns the full config object; PUT accepts any subset
/// of keys to merge.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef CONFIGHANDLER_H
#define CONFIGHANDLER_H

#include "APITypes.h"

/// @brief Return the full device configuration.
APIPBPtr HandlerConfigGet( APIPBPtr pb );

/// @brief Merge a subset of keys into the device configuration.
APIPBPtr HandlerConfigPut( APIPBPtr pb );

#endif // CONFIGHANDLER_H
