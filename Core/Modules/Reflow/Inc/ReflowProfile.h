/// @file ReflowProfile.h
///
/// @brief Reflow profile data structures and filesystem I/O.
///
/// Owns the `ReflowProfile` type and provides load, save, and default-profile
/// access. Profiles are identified by name (filename without path) or by full
/// pathname. When loading by name the module resolves the path internally.
///
/// If no profile file is found, `ReflowProfileGetDefault()` returns a pointer
/// to a hardcoded fallback profile that covers a standard lead-free reflow
/// cycle. This module is a dependency of `Reflow.c`; callers do not need to
/// include it directly unless they are building or inspecting profiles.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef REFLOW_PROFILE_H
#define REFLOW_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "Partition.h"
#include "Types.h"

// ============================================================================
// CONSTANTS
// ============================================================================

/// @brief Maximum number of stages in a reflow profile.
enum { kReflowMaxStages = 8 };

/// @brief Filesystem directory where reflow profiles are stored.
/// Bare filenames passed to ReflowProfileLoad / ReflowProfileSave are
/// resolved relative to this path.
#define REFLOW_PROFILE_DIR    "/" PART_NAME_USER "/profiles"

// ============================================================================
// PROFILE STRUCTURES
// ============================================================================

/// @brief Classification of a reflow profile stage.
typedef enum {
    ReflowStagePreheat = 0, ///< Controlled ramp to soak temperature.
    ReflowStageSoak,        ///< Thermal equalisation hold.
    ReflowStageReflow,      ///< Ramp to peak; hold above liquidus.
    ReflowStageCool,        ///< Controlled or passive cooldown.
} ReflowStageType;

/// @brief Criterion that must be satisfied before advancing to the next stage.
typedef enum {
    ReflowHoldTime,         ///< Hold for a fixed duration once target temperature is reached.
    ReflowHoldTemperature,  ///< Hold until temperature crosses a threshold.
} ReflowHoldCriterion;

/// @brief A single stage within a reflow profile.
typedef struct ReflowStage {
    ReflowStageType     type;          ///< Stage classification — used for display and fault context.
    int16_t             targetTempC;   ///< Target temperature in °C.
    float               rampRateC;     ///< Ramp rate in °C/s. Positive = heat, negative = cool.
    ReflowHoldCriterion holdCriterion; ///< What triggers the advance to the next stage.
    union {
        uint32_t        holdMs;        ///< Hold duration (ReflowHoldTime): milliseconds at target.
        int16_t         holdTempC;     ///< Hold threshold (ReflowHoldTemperature): advance when temp crosses this.
    };
    Permille            fanStart;      ///< Oven fan speed at stage entry (0–1000).
    Permille            fanEnd;        ///< Oven fan speed at stage exit. Equal to fanStart for fixed speed.
    union {
        struct {
            uint8_t heaterTop    : 1;  ///< Enable top heating element.
            uint8_t heaterRear   : 1;  ///< Enable rear convection element.
            uint8_t heaterBottom : 1;  ///< Enable bottom heating element.
            uint8_t              : 5;  ///< Reserved — must be zero.
        };
        uint8_t heaters;               ///< All heater bits as a byte for serialisation.
                                       ///<  Bit layout assumes little-endian (Cortex-M0+): bit0=top, bit1=rear, bit2=bottom.
    };
} ReflowStage, *ReflowStagePtr;

/// @brief A complete reflow profile — a named sequence of stages.
typedef struct ReflowProfile {
    char        name[ 32 ];                 ///< Human-readable profile name.
    uint8_t     stageCount;                 ///< Number of valid entries in `stages`.
    ReflowStage stages[ kReflowMaxStages ]; ///< Stage definitions, in execution order.
} ReflowProfile, *ReflowProfilePtr;

// ============================================================================
// PUBLIC API
// ============================================================================

/// @brief Return a pointer to the hardcoded default lead-free reflow profile.
///
/// The returned pointer is valid for the lifetime of the program.
const ReflowProfile* ReflowProfileGetDefault( void );

/// @brief Load a profile from the filesystem into a caller-supplied buffer.
///
/// @p name may be a bare filename (e.g. `"leaded.rfl"`) or a full pathname.
/// Bare filenames are resolved against the profile storage directory.
///
/// @param[in]  name  Filename or full path of the profile to load.
/// @param[out] out   Caller-supplied buffer to receive the profile data.
/// @return True if the profile was read and validated successfully.
bool ReflowProfileLoad( const char* name, ReflowProfilePtr out );

/// @brief Save a profile to the filesystem.
///
/// @p name may be a bare filename or a full pathname. Bare filenames are
/// written to the profile storage directory.
///
/// @param[in] profile  Profile to save.
/// @param[in] name     Destination filename or full path.
/// @return True if the profile was written successfully.
bool ReflowProfileSave( const ReflowProfile* profile, const char* name );

#endif // REFLOW_PROFILE_H
