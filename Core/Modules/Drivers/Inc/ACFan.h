/// @file ACFan.h
///
/// @brief AC oven circulation fan driver.
///
/// Manages the AC-powered oven fan via a TRIAC channel and an optional AS5600
/// rotary encoder. When FEATURE_ROTARY_ENCODER is enabled, ACFanProcess()
/// updates spinning/stall flags from the encoder; ACFanGetSpeed() returns live
/// RPM. Without an encoder both are unavailable and FlagACFanCalibrationRequired
/// is set, restricting the fan to on/off operation.
///
/// If no calibration file is found at open time, FlagACFanCalibrationRequired
/// is set and speed commands are ignored. Run ACFanRunCalibration() from the
/// ACFanTuning module to produce and persist a profile, then re-open.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef ACFAN_H
#define ACFAN_H

#include "Features.h"

#if FEATURE_OVEN_FAN

#include "Types.h"
#include "SystemStatusFlags.h"
#include "RotaryEncoder.h"
#include "Triac.h"
#if FEATURE_AC_FAN_CALIBRATION
#include "ACFanTuning.h"
#endif // FEATURE_AC_FAN_CALIBRATION

/// @brief Logical identifiers for AC fan instances managed by this driver.
typedef enum {
    OvenFan = 0,   ///< AC induction fan in the oven cavity
    ACFanCount
} ACFanID;

/// @brief Status and diagnostic flag bit positions for an AC fan instance.
/// These map 1:1 to the bits in the per-instance statusHandle event flag group.
typedef enum {
    FlagACFanStatusReady = 0,           ///< Profile loaded and drive is operational
    FlagACFanCalibrationRequired,       ///< No valid profile found; speed commands ignored
    FlagACFanHasEncoder,                ///< An encoder has been attached via ACFanAttachEncoder()
    FlagACFanStatusSpinning,            ///< Encoder confirms rotation above threshold
    FlagACFanStatusStall,               ///< Speed > 0 requested but encoder reads ~0 RPM
    FlagACFanStatusHardwareFault,       ///< TRIAC config error or encoder communication failure
    FlagACFanSpeedPending,              ///< Speed command queued; not yet applied to TRIAC

    ACFanFlagsCount
} ACFanStatusBit;

_Static_assert( ACFanFlagsCount <= 24, "ACFanStatusFlags out of bounds" );

/// @brief Opaque handle to an AC fan instance.
typedef struct ACFanInstance* ACFanRef;

/// @brief Allocate per-instance resources. Does not access hardware or the filesystem.
void     ACFanInitModule( void );

/// @brief Open a handle to a specific AC fan instance.
///
/// On first call for a given ID: opens the TRIAC handle and attempts to load the
/// calibration profile from flash. Sets FlagACFanStatusReady on success or
/// FlagACFanCalibrationRequired if no valid profile is found. Subsequent calls
/// with the same ID return the existing instance without re-opening.
/// Call ACFanAttachEncoder() after ACFanOpen() to enable speed feedback.
///
/// @param[in] id      Fan instance identifier.
/// @param[in] triacID TRIAC channel identifier for the oven fan.
/// @return Handle to the instance, or NULL if @p id is out of range.
ACFanRef ACFanOpen( ACFanID id, TriacID triacID );

#if FEATURE_ROTARY_ENCODER
/// @brief Attach a rotary encoder to a fan instance for speed feedback.
///
/// Looks up the encoder by ID and stores it in the instance. ACFanProcess()
/// will then update spinning/stall flags and ACFanGetSpeed() will return live
/// RPM. Safe to call at any time after ACFanOpen().
///
/// @param[in] fan       Handle returned by ACFanOpen().
/// @param[in] encoderID Rotary encoder identifier for the fan shaft.
void ACFanAttachEncoder( ACFanRef fan, RotaryEncoderID encoderID );
#endif // FEATURE_ROTARY_ENCODER

/// @brief Queue a speed request; applied to the TRIAC by ACFanProcess() on the next tick.
///
/// Ignored if FlagACFanCalibrationRequired is set. Speed is clamped to [0, 1000].
///
/// @param[in] fan   Handle returned by ACFanOpen().
/// @param[in] speed Desired speed in permille of motorMaxRPM (0 = off, 1000 = full).
/// @note Safe to call from any task context.
void     ACFanSetSpeed( ACFanRef fan, Permille speed );

/// @brief Return the most recently measured fan speed from the encoder.
/// @param[in] fan Handle returned by ACFanOpen().
/// @return Cached speed in RPM; 0 if @p fan is NULL or FEATURE_ROTARY_ENCODER is disabled.
/// @note Safe to call from any task context without blocking.
Rpm      ACFanGetSpeed( ACFanRef fan );

/// @brief Return the full status bitmask for this fan instance.
/// @param[in] fan Handle returned by ACFanOpen().
/// @return Bitmask of ACFanStatusBit flags; BIT(FlagACFanStatusHardwareFault) if @p fan is NULL.
uint32_t ACFanGetStatus( ACFanRef fan );

/// @brief Apply any pending speed command and update status flags from the encoder.
/// @warning Do not call from ISR context.
void     ACFanProcess( void );

#if FEATURE_AC_FAN_CALIBRATION
/// @brief Run the full calibration sequence and persist the result for this fan instance.
///
/// Requires an encoder attached via ACFanAttachEncoder() — returns immediately
/// if FlagACFanHasEncoder is not set. On success, writes the calibration profile
/// to /system/fanN.profile (N = instance ID), loads it into the instance, and clears
/// FlagACFanCalibrationRequired.
///
/// @param[in] fan Handle returned by ACFanOpen().
/// @warning Blocks the calling task for up to one hour. Must be called from a dedicated
///          calibration task — not from the reflow engine itself. For accurate results
///          the oven should be at maximum operating temperature; the recommended approach
///          is to run a calibration reflow profile that ramps to that temperature and
///          holds, then signals this function once stable. The reflow engine and all
///          safety monitoring continue to run normally throughout.
/// @return True if calibration succeeded and the fan is ready to accept speed commands;
///         false if aborted, stopped, or the motor failed to spin.
bool     ACFanCalibrate( ACFanRef fan );
#endif // FEATURE_AC_FAN_CALIBRATION

#endif // FEATURE_OVEN_FAN

#endif // ACFAN_H
