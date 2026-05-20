/// @file APIHandlers.c
///
/// @brief Weak default implementations for all REST API route handlers.
///
/// Every handler is declared __weak so that application code can override individual
/// endpoints without modifying this file. Unimplemented handlers release the request
/// PB and return NULL (no response sent).
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"
#include "APICore.h"
#include "types.h"

// Oven control

/// @brief Report the current oven run state and active profile. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with status JSON, or NULL.
__weak APIPBPtr HandlerOvenStatus( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Start a reflow run using the profile specified in the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerOvenRun( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Stop the current reflow run gracefully. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerOvenStop( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Trigger an immediate emergency stop. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerOvenEstop( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// Manual Control

/// @brief Enter manual control mode, allowing direct heater and fan commands. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerManualEnable( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Exit manual control mode and return to safe idle state. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerManualDisable( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Set heater power level in manual control mode. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerManualHeater( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Set cooling fan speed in manual control mode. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerManualFan( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// Sensors

/// @brief Return current temperature readings from all sensors. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with sensor JSON, or NULL.
__weak APIPBPtr HandlerSensorsTemp( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Return mains voltage and ZCD status. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with mains JSON, or NULL.
__weak APIPBPtr HandlerSensorsMains( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
};

// Profiles

/// @brief Return a list of stored reflow profiles. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with profile list JSON, or NULL.
__weak APIPBPtr HandlerProfilesList( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Return a single profile identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with profile JSON, or NULL.
__weak APIPBPtr HandlerProfileGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Create a new reflow profile from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerProfileCreate( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Update an existing profile with data from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerProfileUpdate( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Delete the profile identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerProfileDelete( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
};

// Config

/// @brief Return the device configuration. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with config JSON, or NULL.
__weak APIPBPtr HandlerConfigGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Replace the device configuration with the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerConfigPut( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// Logs

/// @brief Return a list of stored log files. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with log list JSON, or NULL.
__weak APIPBPtr HandlerLogsList( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Return the contents of a single log file. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with log data, or NULL.
__weak APIPBPtr HandlerLogGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Delete a single log file identified by the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerLogDelete( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Delete all stored log files. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerLogsClear( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// Storage

/// @brief Return file system status and free space. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with storage JSON, or NULL.
__weak APIPBPtr HandlerStorageGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Format the storage partition. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
/// @warning Destructive — erases all stored profiles and logs.
__weak APIPBPtr HandlerStorageFormat( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// System

/// @brief Return system status flags, uptime, and firmware version. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with system JSON, or NULL.
__weak APIPBPtr HandlerSystemStatus( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Return the current RTC date and time. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with clock JSON, or NULL.
__weak APIPBPtr HandlerClockGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Set the RTC date and time from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerClockPut( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Perform a software reset of the MCU. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
/// @warning Non-returning under normal conditions.
__weak APIPBPtr HandlerSystemReset( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// Power

/// @brief Return USB PD contract details and live voltage/current readings. @param[in] pb Request PB — caller must ReleasePB(pb). @return Payload with power JSON, or NULL.
__weak APIPBPtr HandlerPowerGet( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

// UI

/// @brief Set the status indicator light state from the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerUiLight( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}

/// @brief Play a buzzer melody or tone specified in the request payload. @param[in] pb Request PB — caller must ReleasePB(pb). @return NULL.
__weak APIPBPtr HandlerUiBuzzer( APIPBPtr pb ) {
    ReleasePB( pb );
    return NULL;
}
