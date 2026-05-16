/// @file ACFanTuning.h
///
/// @brief AC fan calibration and runtime drive engine — public API.
///
/// This module characterises an AC induction fan motor and produces an
/// `ACFanProfileMap` that maps evenly-spaced speed percentages (0–100 %) to the
/// lowest-stress TRIAC drive parameters that achieve each target RPM.
///
/// The caller owns the `ACFanProfileMap`, passes it to `ACFanRunCalibration()`
/// to populate it, and is responsible for persisting it to non-volatile storage.
/// `ACFanDrive()` consumes a previously populated map at runtime.
///
/// Hardware selection (TRIAC channel, rotary encoder instance) is internal to
/// `ACFanTuning.c` and is not exposed here.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef ACFANTUNING_H
#define ACFANTUNING_H

#include <stdbool.h>

#include "Types.h"

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

/// @brief Initialise the AC fan tuning module.
///
/// Acquires the TRIAC channel and rotary encoder handles used internally.
/// Must be called once before `ACFanRunCalibration()` or `ACFanDrive()`.
void ACFanInitCalibration( void );

/// @brief Run the full calibration sequence and populate @p mapOut.
///
/// Executes a two-phase grid search (coarse + fine) for each of the
/// kAcFanNumSteps speed targets, measuring RPM and stress at each
/// candidate TRIAC drive point. The result is written into @p mapOut.
///
/// @param[out] mapOut  Caller-allocated profile map to populate.
///
/// @note Blocks for up to approximately 45 minutes. Must be called from a
///       FreeRTOS task context; uses `osDelay()` throughout.
/// @warning The caller is responsible for persisting @p mapOut to non-volatile
///          storage after this function returns.
void ACFanRunCalibration( ACFanProfileMapPtr mapOut );

/// @brief Drive the fan at the requested speed using a calibrated profile map.
///
/// Looks up the two neighbouring profile slots that bracket @p requestedPm,
/// linearly interpolates all three drive parameters, and applies the result
/// via `TriacRun()`.
///
/// @param[in] map          A previously populated `ACFanProfileMap`.
/// @param[in] requestedPm  Desired speed in permille of `motorMaxRPM` (0–1000).
///
/// @note A value of 0 applies slot 0 (motor off). Values above 1000 are clamped.
void ACFanDrive( const ACFanProfileMapPtr map, Permille requestedPm );

#endif // ACFANTUNING_H
