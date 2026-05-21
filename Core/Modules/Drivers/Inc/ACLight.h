/// @file ACLight.h
///
/// @brief Oven interior light driver.
///
/// Controls the oven cavity light via TRIAC burst-fire at zero-cross. Power is
/// expressed as a permille fraction and mapped linearly to a burst duty cycle,
/// allowing dimming as well as simple on/off operation.
///
/// ACLightProcess() applies any pending power command each tick. Fault conditions
/// from the underlying TRIAC are propagated to FaultFlagsHandle. ACLightGetStatus()
/// returns cached flags safe to call from any context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef ACLIGHT_H
#define ACLIGHT_H

#include "Features.h"

#if FEATURE_OVEN_LIGHT

#include "Types.h"
#include "SystemStatusFlags.h"
#include "Triac.h"

/// @brief Status and diagnostic flag bit positions for the AC light instance.
typedef enum {
    FlagACLightStatusReady = 0,     ///< TRIAC channel opened and ready
    FlagACLightStatusOn,             ///< Power > 0 is currently applied
    FlagACLightStatusHardwareFault,  ///< TRIAC configuration error detected
    FlagACLightPowerPending,         ///< Power command queued; not yet applied to TRIAC

    ACLightFlagsCount
} ACLightStatusBit;

_Static_assert( ACLightFlagsCount <= 24, "ACLightStatusFlags out of bounds" );

/// @brief Logical identifiers for AC light instances.
typedef enum {
    OvenLight1 = 0, ///< Primary oven cavity light
    ACLightCount
} ACLightID;

/// @brief Opaque handle to an AC light instance.
typedef struct ACLightInstance* ACLightRef;

/// @brief Allocate per-instance resources. Does not access hardware.
void      ACLightInitModule( void );

/// @brief Open an AC light instance and acquire its TRIAC channel.
///
/// Idempotent — subsequent calls with the same @p id return the existing handle.
///
/// @param[in] id  Light instance identifier.
/// @return Handle to the instance; NULL if @p id is out of range.
ACLightRef ACLightOpen( ACLightID id );

/// @brief Queue a power request; applied to the TRIAC by ACLightProcess() on the next tick.
///
/// Power is clamped to [0, 100]. Ignored if FlagACLightStatusReady is not set.
///
/// @param[in] light   Handle returned by ACLightOpen().
/// @param[in] percent Desired brightness in percent (0 = off, 100 = full).
/// @note Safe to call from any task context.
void      ACLightSetPower( ACLightRef light, Percent percent );

/// @brief Return the full status bitmask for the light instance.
///
/// @param[in] light  Handle returned by ACLightOpen().
/// @return Bitmask of ACLightStatusBit flags;
///         BIT(FlagACLightStatusHardwareFault) if @p light is NULL.
uint32_t  ACLightGetStatus( ACLightRef light );

/// @brief Apply any pending power command and update status flags.
/// @warning Do not call from ISR context.
void      ACLightProcess( void );

#endif // FEATURE_OVEN_LIGHT

#endif // ACLIGHT_H
