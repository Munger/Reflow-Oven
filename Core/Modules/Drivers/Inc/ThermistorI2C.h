/// @file ThermistorI2C.h
///
/// @brief MCP3221 I2C ADC heatsink thermistor driver.
///
/// Reads a 12-bit ADC value from an MCP3221 over I2C and converts it to a
/// temperature using either a caller-supplied NTC lookup table or a linear
/// approximation. TMI2CProcess() drives the async I2C state machine and updates
/// status flags; getters return cached values safe to call from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef THERMISTORI2C_H
#define THERMISTORI2C_H

#include "Features.h"

#if FEATURE_THERMISTOR_HEATSINK

#include "Types.h"
#include "SystemStatusFlags.h"
#include "I2CManager.h"

/// @brief Logical identifiers for I2C thermistor instances managed by this driver.
typedef enum {
    ThermistorI2C1 = 0,  ///< MCP3221 heatsink thermistor
    ThermistorI2CCount
} ThermistorI2CID;

/// @brief Status and diagnostic flag bit positions for the I2C heatsink thermistor.
/// These map 1:1 to the bits in the private per-instance status event flag group.
typedef enum {
    FlagTMI2CStatusReady = 0,          ///< Module initialised and I2C communication successful
    FlagTMI2CStatusOverTemp,            ///< Heatsink temperature exceeds the 95°C safety limit
    FlagTMI2CStatusOpenCircuit,         ///< ADC reading near maximum — thermistor probe disconnected
    FlagTMI2CStatusShortCircuit,        ///< ADC reading near zero — thermistor probe shorted
    FlagTMI2CStatusHardwareFault,       ///< I2C communication failure (NACK or timeout)
    FlagTMI2CIODone,                    ///< Most recent async I2C read completed successfully.
    FlagTMI2CIOError,                   ///< Most recent async I2C read failed.

    TMI2CFlagsCount
} TMI2CStatusBit;

_Static_assert( TMI2CFlagsCount <= 24, "ThermistorHeatsinkStatusFlags out of bounds" );

/// @brief Opaque handle to a heatsink thermistor instance.
typedef struct ThermistorI2C* ThermistorI2CRef;

/// @brief Allocate per-instance resources. Does not access I2C hardware.
void TMI2CInitModule( void );

/// @brief Open a handle to a specific thermistor instance.
///
/// On first call for a given ID: stores @p i2c and @p ntcTable in the instance.
/// Subsequent calls with the same ID return the existing instance without re-configuring.
///
/// @param[in] id       Thermistor instance identifier.
/// @param[in] i2c      I2C bus handle returned by I2COpen().
/// @param[in] ntcTable 33-entry NTC lookup table (128 ADC counts/step), or NULL for linear approx.
/// @return Handle to the instance, or NULL if @p id is out of range.
ThermistorI2CRef TMI2COpen( ThermistorI2CID id, I2CRef i2c, NTCEntryPtr ntcTable );

/// @brief Return the most recently computed heatsink temperature.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Temperature in milli-degrees Celsius; 0 if @p thermistor is NULL.
/// @note Returns a cached value; safe to call from any task context.
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor );

/// @brief Return the most recently read raw 12-bit ADC value from the MCP3221.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Raw ADC value (0–4095); 0 if @p thermistor is NULL.
/// @note Returns a cached value; safe to call from any task context.
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor );

/// @brief Return the full status bitmask for this thermistor instance.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Bitmask of TMI2CStatusBit flags; safe to call from any task context.
uint32_t TMI2CGetStatus( ThermistorI2CRef thermistor );

/// @brief Drive the I2C state machine, evaluate fault thresholds, and update status flags.
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void TMI2CProcess( void );

#endif // FEATURE_THERMISTOR_HEATSINK

#endif // THERMISTORI2C_H
