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

/// @brief Opaque handle to the AC light instance.
typedef struct ACLightInstance* ACLightRef;

/// @brief Allocate instance resources. Does not access hardware.
void      ACLightInitModule( void );

/// @brief Open the AC light and acquire its TRIAC channel.
///
/// On first call, opens the TRIAC channel and sets FlagACLightStatusReady plus
/// the global DeviceStatusFlagsHandle ready bit. Subsequent calls return the
/// existing instance without re-opening.
///
/// @return Handle to the instance, or NULL on internal error.
ACLightRef ACLightOpen( void );

/// @brief Queue a power request; applied to the TRIAC by ACLightProcess() on the next tick.
///
/// Power is clamped to [0, 1000]. Ignored if FlagACLightStatusReady is not set.
///
/// @param[in] light  Handle returned by ACLightOpen().
/// @param[in] power  Desired power in permille (0 = off, 1000 = full).
/// @note Safe to call from any task context.
void      ACLightSetPower( ACLightRef light, Permille power );

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
