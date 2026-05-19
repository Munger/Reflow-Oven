/// @file ACFanTuning.h
///
/// @brief AC fan calibration and runtime drive engine — public API.
///
/// This module characterises an AC induction fan motor and produces an
/// `ACFanProfileMap` that maps evenly-spaced speed percentages (0–100 %) to the
/// lowest-stress TRIAC drive parameters that achieve each target RPM.
///
/// This is an implementation module — callers should use ACFanCalibrate() from
/// ACFan.h rather than calling ACFanRunCalibration() directly. The caller owns
/// the `ACFanProfileMap` and is responsible for persisting it.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef ACFANTUNING_H
#define ACFANTUNING_H

#include "Features.h"

#if FEATURE_AC_FAN_CALIBRATION

#include <stdbool.h>

#include "Types.h"
#include "RotaryEncoder.h"
#include "Triac.h"

/// @brief Number of calibrated speed steps in the profile, excluding slot 0 (OFF).
///
/// Slots 1 through kAcFanNumSteps correspond to 10%..100% of motorMaxRPM in equal increments.
enum { kAcFanNumSteps = 10 };

/// @brief Internal TRIAC drive parameter set.
///
/// The caller treats this struct as opaque — do not manipulate the fields
/// directly. The runtime drive engine uses it via `ACFanProfileSlot`.
typedef struct {
    uint16_t phaseDelayUs; ///< TRIAC phase-angle delay in microseconds (0 = full power).
    uint8_t  burstOn;      ///< Number of mains half-cycles per burst that fire the gate.
    uint8_t  burstWindow;  ///< Total burst window size in half-cycles (on + off).
} ACFanDriveParams, *ACFanDriveParamsPtr;

/// @brief A single calibrated speed step in the profile map.
typedef struct {
    bool             isFeasible; ///< False if no valid drive strategy was found for this step.
    ACFanDriveParams strategy;   ///< Drive parameters to apply to achieve this speed step.
    Rpm              actualRPM;  ///< Measured RPM achieved by `strategy` during calibration.
} ACFanProfileSlot, *ACFanProfileSlotPtr;

/// @brief Full calibration output produced by `ACFanRunCalibration()`.
///
/// Slot 0 is always motor-off (burstOn = 0).
/// Slots 1 through kAcFanNumSteps correspond to 10%..100% of motorMaxRPM.
typedef struct {
    ACFanProfileSlot slots[ kAcFanNumSteps + 1 ]; ///< Calibrated slot array.
    Rpm              motorMaxRPM;                    ///< Unloaded maximum RPM measured at calibration start.
} ACFanProfileMap, *ACFanProfileMapPtr;

/// @brief Run the full calibration sequence and populate @p mapOut.
///
/// Executes a two-phase grid search (coarse + fine) for each of the
/// kAcFanNumSteps speed targets, measuring RPM and stress at each
/// candidate TRIAC drive point. The result is written into @p mapOut.
///
/// Prefer calling ACFanCalibrate() from ACFan.h rather than this function directly.
///
/// @param[in]  triac   TRIAC handle for the fan being calibrated.
/// @param[in]  encoder Rotary encoder handle attached to the fan shaft.
/// @param[out] mapOut  Caller-allocated profile map to populate.
///
/// @note Blocks for up to approximately 45 minutes. Must be called from a
///       FreeRTOS task context. Responds to FlagReflowStop — on abort the motor
///       is powered off and the function returns with a partial or empty mapOut.
/// @warning The caller is responsible for persisting @p mapOut to non-volatile
///          storage after this function returns.
void ACFanRunCalibration( TriacRef triac, RotaryEncoderRef encoder, ACFanProfileMapPtr mapOut );

#endif // FEATURE_AC_FAN_CALIBRATION

#endif // ACFANTUNING_H
