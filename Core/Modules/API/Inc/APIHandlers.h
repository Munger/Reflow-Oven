/// @file APIHandlers.h
///
/// @brief API handler function declarations.
///
/// Declares the `APIHandler` function-pointer typedef and every handler stub
/// that can be matched by the route table in `apiroutes.h`. All handlers are
/// defined as `__weak` symbols in `apihandlers.c`; application code provides
/// strong overrides to implement actual business logic.
///
/// Handler contract:
///   - Receive a pointer to the matched `APIPB`.
///   - Inspect `pb->rawRequest` for any argument captured from the route pattern.
///   - Inspect `pb->payload` for any request body (e.g. JSON) that arrived after
///     the pattern match.
///   - Write the response status into `pb->status` before returning.
///   - Return a `PayloadPtr` chain (acquired via `AcquirePayload()`) that forms
///     the response body, or `NULL` for an empty body.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APIHANDLERS_H
#define APIHANDLERS_H

#include "APITypes.h"

/// @brief Function-pointer type for all API route handlers.
/// @param[in,out] pb  The matched protocol buffer. The handler writes `pb->status`
///                    and returns a `PayloadPtr` response body chain.
/// @return Head of a Payload chain (owned by the caller), or NULL for no body.
typedef PayloadPtr ( *APIHandler )( APIPBPtr pb );

// Oven control

/// @brief @brief Return current oven status.
PayloadPtr HandlerOvenStatus( APIPBPtr pb );

/// @brief Start a reflow run with the profile named in `pb->rawRequest`.
PayloadPtr HandlerOvenRun( APIPBPtr pb );

/// @brief Stop a running reflow cycle gracefully.
PayloadPtr HandlerOvenStop( APIPBPtr pb );

/// @brief Immediately cut all power to heater and fan (emergency stop).
PayloadPtr HandlerOvenEstop( APIPBPtr pb );

// Manual control

/// @brief Enable manual override mode, allowing direct heater and fan control.
PayloadPtr HandlerManualEnable( APIPBPtr pb );

/// @brief Disable manual override mode and return to automatic control.
PayloadPtr HandlerManualDisable( APIPBPtr pb );

/// @brief Set the heater drive level while in manual mode.
PayloadPtr HandlerManualHeater( APIPBPtr pb );

/// @brief Set the oven fan speed while in manual mode.
PayloadPtr HandlerManualFan( APIPBPtr pb );

// Sensor readings

/// @brief Return all current temperature sensor readings.
PayloadPtr HandlerSensorsTemp( APIPBPtr pb );

/// @brief Return current mains voltage and frequency readings.
PayloadPtr HandlerSensorsMains( APIPBPtr pb );

// Profile management

/// @brief Return a list of all stored reflow profiles.
PayloadPtr HandlerProfilesList( APIPBPtr pb );

/// @brief Return the reflow profile named in `pb->rawRequest`.
PayloadPtr HandlerProfileGet( APIPBPtr pb );

/// @brief Create a new reflow profile with the name in `pb->rawRequest`.
PayloadPtr HandlerProfileCreate( APIPBPtr pb );

/// @brief Update an existing reflow profile named in `pb->rawRequest`.
PayloadPtr HandlerProfileUpdate( APIPBPtr pb );

/// @brief Delete the reflow profile named in `pb->rawRequest`.
PayloadPtr HandlerProfileDelete( APIPBPtr pb );

// Configuration

/// @brief Return the current system configuration.
PayloadPtr HandlerConfigGet( APIPBPtr pb );

/// @brief Apply a new system configuration from the request payload.
PayloadPtr HandlerConfigPut( APIPBPtr pb );

// Log management

/// @brief Return a list of all stored log files.
PayloadPtr HandlerLogsList( APIPBPtr pb );

/// @brief Return the contents of the log file named in `pb->rawRequest`.
PayloadPtr HandlerLogGet( APIPBPtr pb );

/// @brief Delete the log file named in `pb->rawRequest`.
PayloadPtr HandlerLogDelete( APIPBPtr pb );

/// @brief Delete all stored log files.
PayloadPtr HandlerLogsClear( APIPBPtr pb );

// Storage management

/// @brief Return flash storage usage statistics.
PayloadPtr HandlerStorageGet( APIPBPtr pb );

/// @brief Erase and reformat the flash storage filesystem.
PayloadPtr HandlerStorageFormat( APIPBPtr pb );

// System

/// @brief Return system health and build information.
PayloadPtr HandlerSystemStatus( APIPBPtr pb );

/// @brief Return the current real-time clock value.
PayloadPtr HandlerClockGet( APIPBPtr pb );

/// @brief Set the real-time clock from the value in `pb->rawRequest`.
PayloadPtr HandlerClockPut( APIPBPtr pb );

/// @brief Perform a software reset of the microcontroller.
PayloadPtr HandlerSystemReset( APIPBPtr pb );

// Power

/// @brief Return current USB-PD power contract details.
PayloadPtr HandlerPowerGet( APIPBPtr pb );

// UI

/// @brief Set the oven light state from `pb->rawRequest`.
PayloadPtr HandlerUiLight( APIPBPtr pb );

/// @brief Trigger a buzzer pattern identified by `pb->rawRequest`.
PayloadPtr HandlerUiBuzzer( APIPBPtr pb );

#endif // APIHANDLERS_H
