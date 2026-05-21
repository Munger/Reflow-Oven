/// @file ConfigHandler.c
///
/// @brief Handler implementations for the /config endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief Return the full device configuration. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerConfigGet( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Merge a subset of keys into the device configuration. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerConfigPut( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
