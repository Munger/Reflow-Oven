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

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

#include "ACLight.h"

/// @brief Burst window size in AC half-cycles (200 ms at 50 Hz, 167 ms at 60 Hz).
static const uint8_t kBurstWindow = 20U;

/// @brief Internal state for the single AC light instance.
typedef struct ACLightInstance {
    TriacRef         triac;           ///< TRIAC channel handle acquired at ACLightOpen()
    osEventFlagsId_t statusHandle;    ///< Per-instance event flag group
    Permille         requestedPower;  ///< Last power requested via ACLightSetPower()
} ACLightInstance, *ACLightInstancePtr;

static ACLightInstance instance;

static void DriveAtPower( ACLightInstancePtr light, Permille power );

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate instance resources. Does not access hardware.
void ACLightInitModule( void ) {
    memset( &instance, 0, sizeof( instance ) );
    instance.statusHandle = osEventFlagsNew( NULL );
}

/// @brief Open the AC light and acquire its TRIAC channel.
///
/// On first call, opens TriacLight, sets FlagACLightStatusReady, and raises
/// FlagOvenLightReady in DeviceStatusFlagsHandle. Subsequent calls are no-ops.
///
/// @return Handle to the instance, or NULL on internal error.
ACLightRef ACLightOpen( void ) {
    if ( instance.triac == NULL ) {
        instance.triac = TriacOpen( TriacLight );
        osEventFlagsSet( instance.statusHandle,   BIT( FlagACLightStatusReady ) );
        osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagOvenLightReady     ) );
    }

    return &instance;
}

/// @brief Queue a power request; applied to the TRIAC by ACLightProcess() on the next tick.
///
/// Clamps @p power to [0, 1000] and sets FlagACLightPowerPending to signal
/// ACLightProcess(). Safe to call from any task context; the power value is
/// written inside a critical section.
void ACLightSetPower( ACLightRef light, Permille power ) {
    if ( light == NULL ) return;

    uint32_t flags = osEventFlagsGet( instance.statusHandle );
    if ( !( flags & BIT( FlagACLightStatusReady ) ) ) return;

    Permille clamped = ( power > 1000 ) ? 1000 : power;

    taskENTER_CRITICAL();
    instance.requestedPower = clamped;
    taskEXIT_CRITICAL();
    osEventFlagsSet( instance.statusHandle, BIT( FlagACLightPowerPending ) );
}

/// @brief Return the full status bitmask for the light instance.
uint32_t ACLightGetStatus( ACLightRef light ) {
    if ( light == NULL ) return BIT( FlagACLightStatusHardwareFault );
    return osEventFlagsGet( instance.statusHandle );
}

/// @brief Apply any pending power command and update status flags.
///
/// Checks FlagACLightPowerPending each tick. If set, clears the flag, drives the
/// TRIAC at the requested power, then reads back the TRIAC status. A
/// FlagTriacStatusConfigError latches FlagACLightStatusHardwareFault and raises
/// FlagOvenLightFault in FaultFlagsHandle; a clean read clears both.
///
/// @warning Do not call from ISR context.
void ACLightProcess( void ) {
    if ( instance.triac == NULL ) return;

    uint32_t flags = osEventFlagsGet( instance.statusHandle );
    if ( !( flags & BIT( FlagACLightStatusReady ) ) ) return;

    if ( !( flags & BIT( FlagACLightPowerPending ) ) ) return;

    osEventFlagsClear( instance.statusHandle, BIT( FlagACLightPowerPending ) );
    DriveAtPower( &instance, instance.requestedPower );

    if ( TriacGetStatus( instance.triac ) & BIT( FlagTriacStatusConfigError ) ) {
        osEventFlagsSet( instance.statusHandle, BIT( FlagACLightStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle,      BIT( FlagOvenLightFault             ) );
    } else {
        osEventFlagsClear( instance.statusHandle, BIT( FlagACLightStatusHardwareFault ) );
        osEventFlagsClear( FaultFlagsHandle,      BIT( FlagOvenLightFault             ) );
    }

    if ( instance.requestedPower > 0 ) {
        osEventFlagsSet( instance.statusHandle, BIT( FlagACLightStatusOn ) );
    } else {
        osEventFlagsClear( instance.statusHandle, BIT( FlagACLightStatusOn ) );
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
