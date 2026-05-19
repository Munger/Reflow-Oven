/// @file Thermistor.h
///
/// @brief NTC thermistor driver using the STM32 internal ADC DMA buffer.
///
/// Only the channels enabled in Features.h are compiled in. ThermistorCount
/// reflects only the enabled channels; the ADC DMA scan sequence in CubeMX
/// must be configured to match.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef THERMISTOR_H
#define THERMISTOR_H

#include "Features.h"

/// @brief Sentinel value for externalCjtId meaning "no external CJT thermistor —
///        fall back to the MAX31856 chip-internal cold-junction measurement".
///        Always defined regardless of FEATURE_THERMISTORS.
#define kThermistorNone  0xFFu

#if FEATURE_THERMISTORS

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
    FlagTMInvertedDivider,           ///< Divider network is inverted — swap open/short fault polarity

    TMFlagsCount
} TMStatusBit;

_Static_assert( TMFlagsCount <= 24, "Thermistor status flags out of bounds" );

/// @brief Logical identifiers for the enabled NTC thermistor channels.
///
/// Only channels enabled in Features.h appear in this enum. ThermistorCount
/// equals the number of enabled channels; it also equals the ADC DMA scan depth.
typedef enum {
#if FEATURE_THERMISTOR_CJT_1
    ThermistorCJT1,   ///< Cold-junction thermistor for Thermocouple1
#endif // FEATURE_THERMISTOR_CJT_1
#if FEATURE_THERMISTOR_CJT_2
    ThermistorCJT2,   ///< Cold-junction thermistor for Thermocouple2
#endif // FEATURE_THERMISTOR_CJT_2
#if FEATURE_THERMISTOR_OVEN
    ThermistorOven,   ///< Oven cavity NTC (inverted divider network)
#endif // FEATURE_THERMISTOR_OVEN

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

#endif // FEATURE_THERMISTORS

#endif // THERMISTOR_H
