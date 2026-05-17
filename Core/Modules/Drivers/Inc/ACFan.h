/// @file ACFan.h
///
/// @brief AC oven circulation fan driver.
///
/// Manages the AC-powered oven fan via a TRIAC channel and an AS5600 rotary
/// encoder. ACFanProcess() applies pending speed commands using a calibrated
/// profile loaded from flash at ACFanOpen(), and updates status flags from
/// the encoder. Getters return cached values safe to call from any context.
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

#include "Types.h"
#include "SystemStatusFlags.h"
#include "RotaryEncoder.h"
#include "Triac.h"

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
/// On first call for a given ID: opens the TRIAC and encoder handles internally,
/// then attempts to load the calibration profile from flash. Sets
/// FlagACFanStatusReady on success or FlagACFanCalibrationRequired if no valid
/// profile is found. Subsequent calls with the same ID return the existing
/// instance without re-opening.
///
/// @param[in] id        Fan instance identifier.
/// @param[in] triacID   TRIAC channel identifier for the oven fan.
/// @param[in] encoderID Rotary encoder identifier for the fan shaft.
/// @return Handle to the instance, or NULL if @p id is out of range.
ACFanRef ACFanOpen( ACFanID id, TriacID triacID, RotaryEncoderID encoderID );

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
/// @return Cached speed in RPM; 0 if @p fan is NULL.
/// @note Safe to call from any task context without blocking.
Rpm      ACFanGetSpeed( ACFanRef fan );

/// @brief Return the full status bitmask for this fan instance.
/// @param[in] fan Handle returned by ACFanOpen().
/// @return Bitmask of ACFanStatusBit flags; BIT(FlagACFanStatusHardwareFault) if @p fan is NULL.
uint32_t ACFanGetStatus( ACFanRef fan );

/// @brief Apply any pending speed command and update status flags from the encoder.
/// @warning Do not call from ISR context.
void     ACFanProcess( void );

#endif // ACFAN_H
