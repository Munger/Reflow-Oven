/// @file LogsHandler.c
///
/// @brief Handler implementations for the /logs endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief List all stored log files. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerLogsList( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Return the contents of a single log file. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerLogGet( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Delete a single log file by name. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerLogDelete( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Delete all stored log files. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerLogsClear( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
