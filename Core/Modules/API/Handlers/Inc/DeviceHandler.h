/// @file DeviceHandler.h
///
/// @brief Handler declarations for the /devices endpoint group.
///
/// Every device instance on the board is accessible via a uniform
/// GET/PUT interface at "/devices/<type>/<name>". Device handlers
/// extract type and name from the request string via sscanf, then
/// dispatch to the appropriate driver at runtime.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef DEVICEHANDLER_H
#define DEVICEHANDLER_H

#include "APITypes.h"

/// @brief Enumerate all registered device instances.
APIPBPtr HandlerDeviceList( APIPBPtr pb );

/// @brief Read from any device by type+name.
APIPBPtr HandlerDeviceGet( APIPBPtr pb );

/// @brief Write to any device by type+name.
APIPBPtr HandlerDeviceSet( APIPBPtr pb );

#endif // DEVICEHANDLER_H
