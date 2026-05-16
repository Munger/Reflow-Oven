/// @file DCFan.h
///
/// @brief EMC2101 DC fan controller driver.
///
/// Manages a single board-cooling fan via the EMC2101 I2C controller.
/// DCFanProcess() drives the async I2C state machine and updates all cached
/// values; the getters are safe to call from any task context without blocking.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef DCFAN_H
#define DCFAN_H

#include <stdbool.h>

#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Logical identifiers for DC fan channels managed by this driver.
typedef enum {
    BoardCoolingFan = 0   ///< Onboard EMC2101-controlled cooling fan
} DCFanID;

/// @brief Status and diagnostic flag bit positions for the DC fan module.
/// These map 1:1 to the bits in the private boardFanStatus event flag group.
typedef enum {
    FlagDCFanStatusReady = 0,       ///< Fan controller initialised and communicating
    FlagDCFanStatusSpinning,         ///< Tachometer confirms rotation above threshold
    FlagDCFanStatusStall,            ///< Speed > 0 was requested but measured RPM is ~0
    FlagDCFanStatusNoTach,           ///< Open circuit or failed tachometer sensor
    FlagDCFanStatusUnderSpeed,       ///< Measured RPM significantly below requested target
    FlagDCFanStatusOverTemp,         ///< EMC2101 internal or external temperature over limit
    FlagDCFanStatusHardwareFault,    ///< I2C communication failure with the EMC2101

    DCFanFlagsCount
} DCFanStatusBit;

_Static_assert( DCFanFlagsCount <= 24, "BoardFanStatusFlags out of bounds" );

/// @brief Opaque handle to a DC fan controller instance.
typedef struct DCFanController* DCFanRef;

/// @brief Initialise the fan controller and configure the EMC2101 over I2C.
void               DCFanInitModule( void );

/// @brief Open a handle to the specified fan channel.
/// @param[in] fanID Fan channel identifier (currently only BoardCoolingFan).
/// @return Handle to the fan instance, or NULL if the ID is invalid.
DCFanRef           DCFanOpen( DCFanID fanID );

/// @brief Queue a fan speed request; the I2C write is applied by DCFanProcess() on the next tick.
/// @param[in] fan   Handle returned by DCFanOpen().
/// @param[in] speed Target duty cycle in permille (0 = off, 1000 = 100%).
/// @note This function only queues the request. Actual hardware change happens in DCFanProcess().
void               DCFanSetSpeed( DCFanRef fan, Permille speed );

/// @brief Return the most recently measured fan speed.
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Current speed in RPM; returns 0 if @p fan is NULL.
/// @note Returns a cached value; safe to call from any task context.
Rpm                DCFanGetSpeed( DCFanRef fan );

/// @brief Return the EMC2101 internal die temperature (used for over-temperature detection).
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Temperature in milli-degrees Celsius; returns 0 if @p fan is NULL.
/// @note Returns a cached value; safe to call from any task context.
Temperature        DCFanGetInternalTemp( DCFanRef fan );

/// @brief Return the EMC2101 external (remote) thermistor temperature.
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Temperature in milli-degrees Celsius; returns 0 if @p fan is NULL.
/// @note Returns a cached value; safe to call from any task context.
Temperature        DCFanGetExternalTemp( DCFanRef fan );

/// @brief Return the full status bitmask from the private boardFanStatus flags.
/// @return Bitmask of DCFanStatusBit flags; safe to call from any task context.
uint32_t           DCFanGetStatus( void );

/// @brief Run a calibration sweep (for factory use; not called during normal operation).
/// @param[in] fan Handle returned by DCFanOpen().
void               DCFanCalibrate( DCFanRef fan );

/// @brief Drive the I2C state machine, apply pending speed commands, and update status flags.
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void               DCFanProcess( void );

#endif // DCFAN_H
