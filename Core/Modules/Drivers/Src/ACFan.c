/// @file ACFan.c
///
/// @brief AC oven circulation fan driver — implementation.
///
/// ACFanOpen() acquires the TRIAC and encoder handles internally by ID, then
/// attempts to load the calibration profile from "/config/acfan.profile".
/// ACFanProcess() applies pending speed commands via DriveAtSpeed(), which
/// mirrors the interpolation logic from ACFanTuning but uses the instance's
/// own TRIAC handle. Status flags are updated from the encoder's cached state
/// each tick; no direct I2C access occurs here.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_OVEN_FAN

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

#include "ACFan.h"
#if FEATURE_AC_FAN_CALIBRATION
#include "ACFanTuning.h"
#endif // FEATURE_AC_FAN_CALIBRATION
#if FEATURE_FILE_SYSTEM
#include "FSUtils.h"
#endif // FEATURE_FILE_SYSTEM

/// @brief Permille increment between adjacent profile slots (= 1000 / kAcFanNumSteps).
static const Permille kSpeedStepPm = 1000U / kAcFanNumSteps;

/// @brief Internal per-instance state for the AC fan driver.
typedef struct ACFanInstance {
    ACFanID          id;             ///< Instance identifier
    TriacRef         triac;          ///< TRIAC channel handle acquired at ACFanOpen()
#if FEATURE_ROTARY_ENCODER
    RotaryEncoderRef encoder;        ///< Rotary encoder handle acquired at ACFanOpen()
#endif // FEATURE_ROTARY_ENCODER
    osEventFlagsId_t statusHandle;   ///< Per-instance event flag group
    Permille         requestedSpeed; ///< Last speed requested via ACFanSetSpeed()
    ACFanProfileMap  profileMap;     ///< Calibration profile loaded from flash at ACFanOpen()
} ACFanInstance, *ACFanInstancePtr;

/// @brief All AC fan instances — indexed by ACFanID.
static ACFanInstance instances[ ACFanCount ];

static void DriveAtSpeed( ACFanInstancePtr fan, Permille requestedPm );

/// @brief Allocate per-instance resources. Does not access hardware or the filesystem.
void ACFanInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    instances[ OvenFan ].id           = OvenFan;
    instances[ OvenFan ].statusHandle = osEventFlagsNew( NULL );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagOvenFanReady ) );
}

/// @brief Open a handle to a specific AC fan instance.
///
/// On first call for a given ID: opens the TRIAC and encoder handles internally,
/// then attempts to load the calibration profile from flash. Sets
/// FlagACFanStatusReady on success or FlagACFanCalibrationRequired if no valid
/// profile is found. Subsequent calls with the same ID return the existing
/// instance without re-opening.
ACFanRef ACFanOpen( ACFanID id, TriacID triacID ) {
    if ( id >= ACFanCount ) return NULL;
    ACFanInstancePtr fan = &instances[ id ];

    if ( fan->triac == NULL ) {
        fan->triac = TriacOpen( triacID );

#if FEATURE_FILE_SYSTEM
        char   path[ 32 ];
        size_t bytesRead = 0;
        snprintf( path, sizeof( path ), "/system/fan%u.profile", (unsigned)id );
        FSResult res = FSReadFile( path, 0, &fan->profileMap, sizeof( ACFanProfileMap ), &bytesRead );
        if ( res == FSResultOk && bytesRead == sizeof( ACFanProfileMap ) ) {
            osEventFlagsSet( fan->statusHandle, BIT( FlagACFanStatusReady ) );
        } else {
            osEventFlagsSet( fan->statusHandle, BIT( FlagACFanCalibrationRequired ) );
        }
#else
        osEventFlagsSet( fan->statusHandle, BIT( FlagACFanCalibrationRequired ) );
#endif // FEATURE_FILE_SYSTEM
    }

    return fan;
}

#if FEATURE_ROTARY_ENCODER
/// @brief Attach a rotary encoder to a fan instance for speed feedback.
void ACFanAttachEncoder( ACFanRef fan, RotaryEncoderID encoderID ) {
    if ( fan == NULL ) return;
    ACFanInstancePtr inst = (ACFanInstancePtr)fan;
    inst->encoder = REGetRef( encoderID );
    if ( inst->encoder ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagACFanHasEncoder ) );
    }
}
#endif // FEATURE_ROTARY_ENCODER

/// @brief Queue a speed request; applied to the TRIAC by ACFanProcess() on the next tick.
///
/// Ignored if FlagACFanCalibrationRequired is set. Speed is clamped to [0, 1000].
void ACFanSetSpeed( ACFanRef fan, Permille speed ) {
    if ( fan == NULL ) return;

    uint32_t flags = osEventFlagsGet( fan->statusHandle );
    if ( flags & BIT( FlagACFanCalibrationRequired ) ) return;

    Permille clamped = ( speed > 1000 ) ? 1000 : speed;

    taskENTER_CRITICAL();
    fan->requestedSpeed = clamped;
    taskEXIT_CRITICAL();
    osEventFlagsSet( fan->statusHandle, BIT( FlagACFanSpeedPending ) );
}

/// @brief Apply any pending speed command and update status flags from the encoder.
void ACFanProcess( void ) {
    for ( uint8_t i = 0; i < ACFanCount; i++ ) {
        ACFanInstancePtr fan = &instances[ i ];
        if ( fan->triac == NULL ) continue;

        uint32_t flags = osEventFlagsGet( fan->statusHandle );
        if ( !( flags & BIT( FlagACFanStatusReady ) ) ) continue;

        if ( flags & BIT( FlagACFanSpeedPending ) ) {
            osEventFlagsClear( fan->statusHandle, BIT( FlagACFanSpeedPending ) );
            DriveAtSpeed( fan, fan->requestedSpeed );

            if ( TriacGetStatus( fan->triac ) & BIT( FlagTriacStatusConfigError ) ) {
                osEventFlagsSet( fan->statusHandle, BIT( FlagACFanStatusHardwareFault ) );
                osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
            }
        }

#if FEATURE_ROTARY_ENCODER
        if ( flags & BIT( FlagACFanHasEncoder ) ) {
            uint32_t encoderStatus = REGetStatus( fan->encoder );
            uint32_t set = 0, clear = 0;

            if ( encoderStatus & BIT( FlagREStatusHardwareFault ) ) {
                set |= BIT( FlagACFanStatusHardwareFault );
            } else {
                clear |= BIT( FlagACFanStatusHardwareFault );
            }

            if ( encoderStatus & BIT( FlagREStatusSpinning ) ) {
                set   |= BIT( FlagACFanStatusSpinning );
                clear |= BIT( FlagACFanStatusStall );
            } else {
                clear |= BIT( FlagACFanStatusSpinning );
                if ( fan->requestedSpeed > 0 ) {
                    set |= BIT( FlagACFanStatusStall );
                } else {
                    clear |= BIT( FlagACFanStatusStall );
                }
            }

            osEventFlagsSet( fan->statusHandle, set );
            osEventFlagsClear( fan->statusHandle, clear );
        }
#endif // FEATURE_ROTARY_ENCODER

        uint32_t updated = osEventFlagsGet( fan->statusHandle );
        if ( updated & ( BIT( FlagACFanStatusStall ) | BIT( FlagACFanStatusHardwareFault ) ) ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        }
    }
}

/// @brief Return the most recently measured fan speed from the encoder.
Rpm ACFanGetSpeed( ACFanRef fan ) {
#if FEATURE_ROTARY_ENCODER
    return fan ? REGetVelocity( fan->encoder ) : 0;
#else
    UNUSED( fan );
    return 0;
#endif // FEATURE_ROTARY_ENCODER
}

/// @brief Return the full status bitmask for this fan instance.
uint32_t ACFanGetStatus( ACFanRef fan ) {
    if ( fan == NULL ) return BIT( FlagACFanStatusHardwareFault );
    return osEventFlagsGet( fan->statusHandle );
}

#if FEATURE_AC_FAN_CALIBRATION
/// @brief Run the full calibration sequence and persist the result for this fan instance.
/// @return True if calibration succeeded and the fan is ready; false otherwise.
bool ACFanCalibrate( ACFanRef fan ) {
    if ( !fan ) return false;
    ACFanInstancePtr inst = (ACFanInstancePtr)fan;
    if ( !( osEventFlagsGet( inst->statusHandle ) & BIT( FlagACFanHasEncoder ) ) ) return false;

    ACFanProfileMap map;
    ACFanRunCalibration( inst->triac, inst->encoder, &map );

    if ( map.motorMaxRPM == 0 ) return false;

    inst->profileMap = map;
    osEventFlagsClear( inst->statusHandle, BIT( FlagACFanCalibrationRequired ) );
    osEventFlagsSet( inst->statusHandle, BIT( FlagACFanStatusReady ) );

#if FEATURE_FILE_SYSTEM
    char path[ 32 ];
    snprintf( path, sizeof( path ), "/system/fan%u.profile", (unsigned)inst->id );
    FSEnsureDir( "/system", 0 );
    FSWriteFile( path, 0, &map, sizeof( map ) );
#endif // FEATURE_FILE_SYSTEM

    return true;
}
#endif // FEATURE_AC_FAN_CALIBRATION

// ============================================================================
// Internal — TRIAC drive
// ============================================================================

/// @brief Linearly interpolate between @p low and @p high at fractional position @p fractionPm.
static int32_t Interp( int32_t low, int32_t high, Permille fractionPm ) {
    return low + ( ( ( high - low ) * (int32_t)fractionPm ) / 1000 );
}

/// @brief Interpolate the calibrated profile and apply the result to the TRIAC.
///
/// Mirrors the logic in ACFanDrive() from ACFanTuning but uses the instance's
/// own TRIAC handle rather than the tuning module's internal static.
static void DriveAtSpeed( ACFanInstancePtr fan, Permille requestedPm ) {
    ACFanProfileMapPtr map = &fan->profileMap;

    if ( requestedPm == 0 ) {
        TriacOff( fan->triac );
        return;
    }

    if ( requestedPm >= 1000 ) {
        TriacOn( fan->triac );
        return;
    }

    uint8_t lowerIdx = (uint8_t)( requestedPm / kSpeedStepPm );
    uint8_t upperIdx = lowerIdx + 1;

    if ( ( requestedPm % kSpeedStepPm ) == 0 ) {
        TriacDriveParams p;
        p.phaseDelayUs = map->slots[ lowerIdx ].strategy.phaseDelayUs;
        p.burstOn      = map->slots[ lowerIdx ].strategy.burstOn;
        p.burstWindow  = map->slots[ lowerIdx ].strategy.burstWindow;
        TriacRun( fan->triac, p );
        return;
    }

    ACFanDriveParamsPtr low  = &map->slots[ lowerIdx ].strategy;
    ACFanDriveParamsPtr high = &map->slots[ upperIdx ].strategy;

    // Fractional position within the band in permille (0–1000)
    Permille fraction = (Permille)( ( requestedPm % kSpeedStepPm ) * 10U );

    // Snap rather than interpolate across the on/off boundary
    if ( ( low->burstOn == 0 ) != ( high->burstOn == 0 ) ) {
        ACFanDriveParamsPtr chosen = ( fraction < 500 ) ? low : high;
        TriacDriveParams p;
        p.phaseDelayUs = chosen->phaseDelayUs;
        p.burstOn      = chosen->burstOn;
        p.burstWindow  = chosen->burstWindow;
        TriacRun( fan->triac, p );
        return;
    }

    TriacDriveParams p;
    p.phaseDelayUs = (uint16_t)Interp( (int32_t)low->phaseDelayUs, (int32_t)high->phaseDelayUs, fraction );
    p.burstOn      = (uint8_t) Interp( (int32_t)low->burstOn,      (int32_t)high->burstOn,      fraction );
    p.burstWindow  = (uint8_t) Interp( (int32_t)low->burstWindow,  (int32_t)high->burstWindow,  fraction );

    // Guard against burstOn > burstWindow due to independent rounding
    if ( p.burstOn > p.burstWindow ) p.burstOn = p.burstWindow;

    TriacRun( fan->triac, p );
}

#endif // FEATURE_OVEN_FAN
