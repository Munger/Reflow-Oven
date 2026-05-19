/// @file ACLight.c
///
/// @brief Oven interior light driver — implementation.
///
/// ACLightOpen() acquires the TRIAC handle internally using the fixed TriacLight
/// channel. ACLightProcess() applies pending power commands via DriveAtPower(),
/// which uses burst-fire at zero-cross (phaseDelayUs = 0) with a fixed window of
/// kBurstWindow half-cycles. Power 0 maps to TriacOff(); power 1000 maps to
/// TriacOn(); intermediate values scale burstOn linearly. TRIAC fault state is
/// propagated to FaultFlagsHandle each tick.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_OVEN_LIGHT

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

#include "ACLight.h"

/// @brief Burst window size in AC half-cycles (200 ms at 50 Hz, 167 ms at 60 Hz).
static const uint8_t kBurstWindow = 20U;

/// @brief Internal state for one AC light instance.
typedef struct ACLightInstance {
    TriacRef         triac;           ///< TRIAC channel handle acquired at ACLightOpen()
    osEventFlagsId_t statusHandle;    ///< Per-instance event flag group
    Permille         requestedPower;  ///< Last power requested via ACLightSetPower()
} ACLightInstance, *ACLightInstancePtr;

/// @brief All AC light instances — indexed by ACLightID.
static ACLightInstance instances[ ACLightCount ];

static void DriveAtPower( ACLightInstancePtr light, Permille power );

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate per-instance resources. Does not access hardware.
void ACLightInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    for ( uint8_t i = 0; i < ACLightCount; i++ ) {
        instances[ i ].statusHandle = osEventFlagsNew( NULL );
    }
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagOvenLightReady ) );
}

/// @brief Open an AC light instance and acquire its TRIAC channel.
///
/// Idempotent — subsequent calls with the same @p id return the existing handle.
ACLightRef ACLightOpen( ACLightID id ) {
    if ( id >= ACLightCount ) return NULL;
    ACLightInstancePtr light = &instances[ id ];

    if ( light->triac != NULL ) return light;

    light->triac = TriacOpen( TriacLight );
    osEventFlagsSet( light->statusHandle, BIT( FlagACLightStatusReady ) );

    return light;
}

/// @brief Queue a power request; applied to the TRIAC by ACLightProcess() on the next tick.
///
/// Clamps @p power to [0, 1000] and sets FlagACLightPowerPending to signal
/// ACLightProcess(). Safe to call from any task context; the power value is
/// written inside a critical section.
void ACLightSetPower( ACLightRef light, Permille power ) {
    if ( light == NULL ) return;
    ACLightInstancePtr inst = (ACLightInstancePtr)light;

    if ( !( osEventFlagsGet( inst->statusHandle ) & BIT( FlagACLightStatusReady ) ) ) return;

    Permille clamped = ( power > 1000 ) ? 1000 : power;

    taskENTER_CRITICAL();
    inst->requestedPower = clamped;
    taskEXIT_CRITICAL();
    osEventFlagsSet( inst->statusHandle, BIT( FlagACLightPowerPending ) );
}

/// @brief Return the full status bitmask for the light instance.
uint32_t ACLightGetStatus( ACLightRef light ) {
    if ( light == NULL ) return BIT( FlagACLightStatusHardwareFault );
    return osEventFlagsGet( ( (ACLightInstancePtr)light )->statusHandle );
}

/// @brief Apply any pending power command and update status flags for all instances.
///
/// Checks FlagACLightPowerPending each tick. If set, clears the flag, drives the
/// TRIAC at the requested power, then reads back the TRIAC status. A
/// FlagTriacStatusConfigError latches FlagACLightStatusHardwareFault and raises
/// FlagOvenLightFault in FaultFlagsHandle; a clean read clears both.
///
/// @warning Do not call from ISR context.
void ACLightProcess( void ) {
    for ( uint8_t i = 0; i < ACLightCount; i++ ) {
        ACLightInstancePtr inst = &instances[ i ];

        if ( inst->triac == NULL ) continue;

        uint32_t flags = osEventFlagsGet( inst->statusHandle );
        if ( !( flags & BIT( FlagACLightStatusReady   ) ) ) continue;
        if ( !( flags & BIT( FlagACLightPowerPending  ) ) ) continue;

        osEventFlagsClear( inst->statusHandle, BIT( FlagACLightPowerPending ) );
        DriveAtPower( inst, inst->requestedPower );

        if ( TriacGetStatus( inst->triac ) & BIT( FlagTriacStatusConfigError ) ) {
            osEventFlagsSet( inst->statusHandle, BIT( FlagACLightStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle,   BIT( FlagOvenLightFault             ) );
        } else {
            osEventFlagsClear( inst->statusHandle, BIT( FlagACLightStatusHardwareFault ) );
            osEventFlagsClear( FaultFlagsHandle,   BIT( FlagOvenLightFault             ) );
        }

        if ( inst->requestedPower > 0 ) {
            osEventFlagsSet( inst->statusHandle, BIT( FlagACLightStatusOn ) );
        } else {
            osEventFlagsClear( inst->statusHandle, BIT( FlagACLightStatusOn ) );
        }
    }
}

// ============================================================================
// Internal — TRIAC drive
// ============================================================================

/// @brief Apply the requested power level to the TRIAC via burst firing.
///
/// Uses zero-cross burst fire (phaseDelayUs = 0). burstOn is rounded to the
/// nearest half-cycle and clamped to at least 1 for any non-zero power request,
/// so a very low permille value produces one half-cycle on rather than silent off.
///
/// @param[in] light      Instance pointer (always non-NULL at call site).
/// @param[in] power      Power in permille (0 = off, 1000 = full).
static void DriveAtPower( ACLightInstancePtr light, Permille power ) {
    if ( power == 0 ) {
        TriacOff( light->triac );
        return;
    }

    if ( power >= 1000 ) {
        TriacOn( light->triac );
        return;
    }

    uint8_t burstOn = (uint8_t)( ( power * kBurstWindow + 500U ) / 1000U );
    if ( burstOn == 0 ) burstOn = 1;

    TriacDriveParams p;
    p.phaseDelayUs = 0;
    p.burstOn      = burstOn;
    p.burstWindow  = kBurstWindow;
    TriacRun( light->triac, p );
}

#endif // FEATURE_OVEN_LIGHT
