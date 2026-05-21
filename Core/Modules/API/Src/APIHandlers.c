/// @file APIHandlers.c
///
/// @brief Shared response helpers for all API route handlers.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"
#include "APICore.h"

/// @brief Build a status-only response PB inheriting syntax and terminator from @p request.
///
/// Handlers use this to produce simple error responses (400, 404, 501, 503, etc.)
/// without manually acquiring and configuring a PB.
APIPBPtr APIResponseStatus( APIPBPtr request, APIStatus status ) {
    APIPBPtr resp = AcquirePB();
    if ( resp ) {
        resp->status     = status;
        resp->syntax     = request->syntax;
        resp->terminator = request->terminator;
    }
    return resp;
}
