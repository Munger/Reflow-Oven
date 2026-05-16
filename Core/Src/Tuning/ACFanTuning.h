#ifndef ACFANTUNING_H
#define ACFANTUNING_H

// ============================================================================
// ACFanTuning.h
//
// AC Fan Calibration and Runtime Drive Engine
//
// This module characterises an AC fan motor and produces a ProfileMap that
// maps evenly-spaced speed percentages (0–100%) to the lowest-stress TRIAC
// drive parameters that achieve each target RPM.
//
// The caller owns the ACFanProfileMap, passes it to ACFan_RunCalibration()
// to populate it, and is responsible for persisting it to non-volatile storage.
// ACFanDrive() consumes a previously populated map at runtime.
//
// All hardware selection (TRIAC channel, encoder instance) is internal to
// ACFanTuning.c and is not exposed here.
// ============================================================================

#include <stdbool.h>

#include "types.h"

// Number of speed steps in the profile (excluding slot 0 which is always OFF).
#define AC_FAN_NUM_STEPS 10

// Internal representation of TRIAC drive parameters.
// The caller treats this as opaque — do not manipulate fields directly.
typedef struct {
    uint16_t phaseDelayUs;
    uint8_t  burstOn;
    uint8_t  burstWindow;
} ACFanDriveParams, *ACFanDriveParamsPtr;

// A single calibrated speed step.
typedef struct {
    bool             isFeasible; // FALSE if no valid strategy was found for this step
    ACFanDriveParams strategy;    // Drive parameters to apply
    Rpm              actualRPM;  // Measured RPM achieved by this strategy
} ACFanProfileSlot, *ACFanProfileSlotPtr;

// The full calibration output. Slot 0 is always motor-off.
// Slots 1..AC_FAN_NUM_STEPS correspond to 10%..100% of motorMaxRPM.
typedef struct {
    ACFanProfileSlot slots[ AC_FAN_NUM_STEPS + 1 ];
    Rpm              motorMaxRPM;
} ACFanProfileMap, *ACFanProfileMapPtr;

// Initialise the module. Must be called once before any other function.
void ACFan_Init( void );

// Run the full calibration sequence and populate mapOut.
// Blocks for up to approximately 45 minutes.
// Intended to be called from a FreeRTOS task.
void ACFan_RunCalibration( ACFanProfileMapPtr mapOut );

// Drive the fan at requestedPm (0–1000 permille of motorMaxRPM).
// Requires a previously populated map.
void ACFanDrive( const ACFanProfileMapPtr map, Permille requestedPm );

#endif // ACFANTUNING_H