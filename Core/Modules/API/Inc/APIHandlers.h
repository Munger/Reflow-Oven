/// @file APIHandlers.h
///
/// @brief Umbrella header including all per-group handler declarations.
///
/// The route table in APIRoutes.h includes this header to resolve handler
/// function pointer types. Application code may include individual handler
/// headers or this umbrella for convenience.
///
/// All handlers follow the same contract:
///   APIPBPtr handler( APIPBPtr pb );
/// The handler reads the request from @p pb, builds a response by writing
/// into a newly acquired APIPB, and returns it. The caller (APITaskLoop)
/// releases the request PB separately.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APIHANDLERS_H
#define APIHANDLERS_H

#include "Platform.h"
#include "APITypes.h"

/// @brief Build a status-only response PB inheriting syntax and terminator from @p request.
///
/// Acquires a new APIPB, sets its status and copies syntax/terminator from the
/// incoming request so the serialiser produces a correctly-formatted response.
/// The caller owns the returned PB and must queue or release it.
///
/// @param[in] request  The incoming request PB (syntax/terminator source).
/// @param[in] status   HTTP-like status code to set on the response.
/// @return A new APIPB with the given @p status, or NULL if the pool is exhausted.
APIPBPtr APIResponseStatus( APIPBPtr request, APIStatus status );

/// @brief Function-pointer type for all API route handlers.
typedef APIPBPtr ( *APIHandler )( APIPBPtr pb );

#include "DeviceHandler.h"
#include "ReflowHandler.h"
#include "ProfileHandler.h"
#include "ConfigHandler.h"
#include "LogsHandler.h"
#include "StorageHandler.h"
#include "FileHandler.h"
#include "SystemHandler.h"

#endif // APIHANDLERS_H
