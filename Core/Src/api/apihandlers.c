/// @file APIHandlers.c
///
/// @brief Weak default implementations for all REST API route handlers.
///
/// Every handler is declared __weak so that application code can override individual
/// endpoints without modifying this file. Unimplemented handlers return NULL, which
/// the API task interprets as "no payload" — the route still receives a response with
/// whatever status was set on the request PB (defaulting to API_STATUS_OK).
///
/// Handlers receive ownership of @p pb and must call ReleasePB(pb) before returning.
/// The returned PayloadPtr (if non-NULL) is attached to the response and released
/// by the serialiser after transmission.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"
#include "types.h"

// Oven control

/// @brief Report the current oven run state and active profile. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with status JSON, or NULL.
__weak PayloadPtr HandlerOvenStatus( APIPBPtr pb ) {
    return NULL;
}

/// @brief Start a reflow run using the profile specified in the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerOvenRun( APIPBPtr pb ) {
    return NULL;
}

/// @brief Stop the current reflow run gracefully. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerOvenStop( APIPBPtr pb ) {
    return NULL;
}

/// @brief Trigger an immediate emergency stop. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerOvenEstop( APIPBPtr pb ) {
    return NULL;
}

// Manual Control

/// @brief Enter manual control mode, allowing direct heater and fan commands. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerManualEnable( APIPBPtr pb ) {
    return NULL;
}

/// @brief Exit manual control mode and return to safe idle state. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerManualDisable( APIPBPtr pb ) {
    return NULL;
}

/// @brief Set heater power level in manual control mode. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerManualHeater( APIPBPtr pb ) {
    return NULL;
}

/// @brief Set cooling fan speed in manual control mode. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerManualFan( APIPBPtr pb ) {
    return NULL;
}

// Sensors

/// @brief Return current temperature readings from all sensors. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with sensor JSON, or NULL.
__weak PayloadPtr HandlerSensorsTemp( APIPBPtr pb ) {
    return NULL;
}

/// @brief Return mains voltage and ZCD status. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with mains JSON, or NULL.
__weak PayloadPtr HandlerSensorsMains( APIPBPtr pb ) {
    return NULL;
};

// Profiles

/// @brief Return a list of stored reflow profiles. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with profile list JSON, or NULL.
__weak PayloadPtr HandlerProfilesList( APIPBPtr pb ) {
    return NULL;
}

/// @brief Return a single profile identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with profile JSON, or NULL.
__weak PayloadPtr HandlerProfileGet( APIPBPtr pb ) {
    return NULL;
}

/// @brief Create a new reflow profile from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerProfileCreate( APIPBPtr pb ) {
    return NULL;
}

/// @brief Update an existing profile with data from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerProfileUpdate( APIPBPtr pb ) {
    return NULL;
}

/// @brief Delete the profile identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerProfileDelete( APIPBPtr pb ) {
    return NULL;
};

// Config

/// @brief Return the device configuration. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with config JSON, or NULL.
__weak PayloadPtr HandlerConfigGet( APIPBPtr pb ) {
    return NULL;
}

/// @brief Replace the device configuration with the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerConfigPut( APIPBPtr pb ) {
    return NULL;
}

// Logs

/// @brief Return a list of stored log files. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with log list JSON, or NULL.
__weak PayloadPtr HandlerLogsList( APIPBPtr pb ) {
    return NULL;
}

/// @brief Return the contents of a single log file. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with log data, or NULL.
__weak PayloadPtr HandlerLogGet( APIPBPtr pb ) {
    return NULL;
}

/// @brief Delete a single log file identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerLogDelete( APIPBPtr pb ) {
    return NULL;
}

/// @brief Delete all stored log files. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerLogsClear( APIPBPtr pb ) {
    return NULL;
}

// Storage

/// @brief Return file system status and free space. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with storage JSON, or NULL.
__weak PayloadPtr HandlerStorageGet( APIPBPtr pb ) {
    return NULL;
}

/// @brief Format the storage partition. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
/// @warning Destructive — erases all stored profiles and logs.
__weak PayloadPtr HandlerStorageFormat( APIPBPtr pb ) {
    return NULL;
}

// System

/// @brief Return system status flags, uptime, and firmware version. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with system JSON, or NULL.
__weak PayloadPtr HandlerSystemStatus( APIPBPtr pb ) {
    return NULL;
}

/// @brief Return the current RTC date and time. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with clock JSON, or NULL.
__weak PayloadPtr HandlerClockGet( APIPBPtr pb ) {
    return NULL;
}

/// @brief Set the RTC date and time from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerClockPut( APIPBPtr pb ) {
    return NULL;
}

/// @brief Perform a software reset of the MCU. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
/// @warning Non-returning under normal conditions.
__weak PayloadPtr HandlerSystemReset( APIPBPtr pb ) {
    return NULL;
}

// Power

/// @brief Return USB PD contract details and live voltage/current readings. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with power JSON, or NULL.
__weak PayloadPtr HandlerPowerGet( APIPBPtr pb ) {
    return NULL;
}

// UI

/// @brief Set the status indicator light state from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerUiLight( APIPBPtr pb ) {
    return NULL;
}

/// @brief Play a buzzer melody or tone specified in the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak PayloadPtr HandlerUiBuzzer( APIPBPtr pb ) {
    return NULL;
}
