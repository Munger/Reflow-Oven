/// @file Thermistor.h
///
/// @brief NTC thermistor driver using the STM32 internal ADC DMA buffer.
///
/// Manages three NTC thermistor channels (two cold-junction, one oven cavity).
/// Each instance uses a lookup table for linearisation. Per-instance status flags
/// are private to thermistor.c. TMGetTemperature() returns a cached, interpolated
/// value safe to call from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef THERMISTOR_H
#define THERMISTOR_H

#include "SystemStatusFlags.h"
#include "Types.h"

/// @brief Status and diagnostic flag bit positions for an NTC thermistor channel.
/// These map 1:1 to the bits in each instance's private event flag group.
typedef enum {
    FlagTMStatusReady = 0,          ///< Module initialised and ADC DMA is running
    FlagTMStatusLowTemp,             ///< Temperature below the plausible range (< −30°C)
    FlagTMStatusHighTemp,            ///< Temperature above the safety limit (> 280°C)
    FlagTMStatusOpenCircuit,         ///< ADC value near the supply rail — probe disconnected
    FlagTMStatusShortCircuit,        ///< ADC value near ground — probe shorted
    FlagTMStatusHardwareFault,       ///< Internal ADC or DMA peripheral error

    TMFlagsCount
} TMStatusBit;

_Static_assert( TMFlagsCount <= 24, "Thermistor status flags out of bounds" );

/// @brief Logical identifiers for the three NTC thermistor channels.
typedef enum {
    ThermistorCJT1 = 0,   ///< Cold-junction thermistor for Thermocouple1
    ThermistorCJT2,        ///< Cold-junction thermistor for Thermocouple2
    ThermistorOven,        ///< Oven cavity NTC (inverted divider network)

    ThermistorCount
} ThermistorID;

/// @brief Opaque handle to a thermistor instance.
typedef struct Thermistor* ThermistorRef;

/// @brief Initialise the NTC module, create per-instance flag groups, and start the ADC DMA.
void TMInitModule( void );

/// @brief Open a handle to a specific NTC thermistor channel.
/// @param[in] thermistorID Channel identifier.
/// @return Handle to the instance, or NULL if @p thermistorID is out of range.
ThermistorRef TMOpen( ThermistorID thermistorID );

/// @brief Return the interpolated temperature for a thermistor channel.
///
/// Reads the current ADC DMA value and interpolates between two lookup table
/// entries using the fractional position within the 128-count step.
///
/// @param[in] thermistor Handle returned by TMOpen().
/// @return Temperature in milli-degrees Celsius; 0 if @p thermistor is NULL.
/// @note Pure computation from the DMA buffer — safe to call from any task context.
Temperature TMGetTemperature( ThermistorRef thermistor );

/// @brief Return the current status bitmask for a specific thermistor instance.
/// @param[in] thermistor Handle returned by TMOpen().
/// @return Bitmask of TMStatusBit flags; 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
uint32_t TMGetStatus( ThermistorRef thermistor );

/// @brief Process all thermistor channels: evaluate fault thresholds and update status flags.
///
/// Reads each channel's current ADC value, checks open/short circuit margins and
/// temperature plausibility limits, and maps results to per-instance status flags.
/// Propagates channel-specific faults to FaultFlagsHandle.
///
/// @warning Must be called from task context — reads HAL ADC state.
void TMProcess( void );

#endif // THERMISTOR_H
