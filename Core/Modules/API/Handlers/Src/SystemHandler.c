/// @file SystemHandler.c
///
/// @brief Handler implementations for the /system endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief Return system status (flags, uptime, firmware version). @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerSystemStatus( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Return USB PD / power rail status. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerSystemPower( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Gracefully stop all hot-side drivers and isolate power. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerSystemStop( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Return the current RTC date and time. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerClockGet( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Set the RTC date and time from the request payload. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerClockPut( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Enable or disable debug mode (extra flag detail in device GET responses). @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerSystemDebug( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Return API pool utilisation statistics. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerSystemStats( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Software reset of the MCU. @param[in] pb Request PB owned by caller. @return NULL.
/// @warning Non-returning under normal conditions.
APIPBPtr HandlerSystemReset( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
