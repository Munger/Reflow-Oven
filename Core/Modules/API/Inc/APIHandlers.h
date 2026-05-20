/// @file APIHandlers.h
///
/// @brief API handler function declarations.
///
/// Declares the `APIHandler` function-pointer typedef and every handler stub
/// that can be matched by the route table in `apiroutes.h`. All handlers are
/// defined as `__weak` symbols in `apihandlers.c`; application code provides
/// strong overrides to implement actual business logic.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APIHANDLERS_H
#define APIHANDLERS_H

#include "APITypes.h"

/// @brief Function-pointer type for all API route handlers.
///
/// The handler reads the request from @p pb, builds a response by allocating
/// a new APIPB from the pool, and returns it. The caller releases the request
/// PB separately.
///
/// @param[in] pb  The matched protocol buffer (owned by the caller).
/// @return A new APIPB with status, syntax, terminator, and optional payload
///         set, or NULL if no response should be sent.
typedef APIPBPtr ( *APIHandler )( APIPBPtr pb );

// Oven control

APIPBPtr HandlerOvenStatus( APIPBPtr pb );
APIPBPtr HandlerOvenRun( APIPBPtr pb );
APIPBPtr HandlerOvenStop( APIPBPtr pb );
APIPBPtr HandlerOvenEstop( APIPBPtr pb );

APIPBPtr HandlerManualEnable( APIPBPtr pb );
APIPBPtr HandlerManualDisable( APIPBPtr pb );
APIPBPtr HandlerManualHeater( APIPBPtr pb );
APIPBPtr HandlerManualFan( APIPBPtr pb );

APIPBPtr HandlerSensorsTemp( APIPBPtr pb );
APIPBPtr HandlerSensorsMains( APIPBPtr pb );

APIPBPtr HandlerProfilesList( APIPBPtr pb );
APIPBPtr HandlerProfileGet( APIPBPtr pb );
APIPBPtr HandlerProfileCreate( APIPBPtr pb );
APIPBPtr HandlerProfileUpdate( APIPBPtr pb );
APIPBPtr HandlerProfileDelete( APIPBPtr pb );

APIPBPtr HandlerConfigGet( APIPBPtr pb );
APIPBPtr HandlerConfigPut( APIPBPtr pb );

APIPBPtr HandlerLogsList( APIPBPtr pb );
APIPBPtr HandlerLogGet( APIPBPtr pb );
APIPBPtr HandlerLogDelete( APIPBPtr pb );
APIPBPtr HandlerLogsClear( APIPBPtr pb );

APIPBPtr HandlerStorageGet( APIPBPtr pb );
APIPBPtr HandlerStorageFormat( APIPBPtr pb );

APIPBPtr HandlerSystemStatus( APIPBPtr pb );
APIPBPtr HandlerClockGet( APIPBPtr pb );
APIPBPtr HandlerClockPut( APIPBPtr pb );
APIPBPtr HandlerSystemReset( APIPBPtr pb );

APIPBPtr HandlerPowerGet( APIPBPtr pb );

APIPBPtr HandlerUiLight( APIPBPtr pb );
APIPBPtr HandlerUiBuzzer( APIPBPtr pb );

#endif // APIHANDLERS_H
