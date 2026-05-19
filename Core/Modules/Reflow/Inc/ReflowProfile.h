/// @file ReflowProfile.h
///
/// @brief Reflow profile data structures and filesystem I/O.
///
/// Owns the `ReflowProfile` and `ReflowStage` types. Stages form a
/// singly-linked list; the profile holds the head pointer. Both are
/// heap-allocated by `ReflowProfileLoad()` and must be released with
/// `ReflowProfileFree()` when the cycle ends.
///
/// On-disk format is plain ASCII tagged CSV — endian-agnostic and transferable
/// verbatim over a CLI link. Each line is a comma-separated list of `xx=value`
/// fields (2-char tag). A line containing `ty=` is a stage record; all other
/// tags update the profile header. Unrecognised tags are discarded; absent
/// stage fields take defined defaults. See ReflowProfile.c for the full tag
/// table and format version history.
///
/// If `name` is NULL or the file is not found, `ReflowProfileLoad()` returns
/// a heap copy of the built-in lead-free default profile.
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

/// @brief Filesystem directory where reflow profiles are stored.
/// Bare filenames passed to ReflowProfileLoad / ReflowProfileSave are
/// resolved relative to this path.
#define REFLOW_PROFILE_DIR    "/" PART_NAME_USER "/profiles"

/// @brief Maximum stage name length, excluding NUL terminator.
enum { kStageNameLen = 16 };

// ============================================================================
// PROFILE STRUCTURES
// ============================================================================

/// @brief Optional special action triggered when this stage enters its hold phase.
typedef enum {
    ReflowFnNone = 0,      ///< Normal hold — time or temperature criterion only.
    ReflowFnCalFan,        ///< Run AC fan calibration sequence during hold.
    ReflowFnCalThermal,    ///< Run thermal calibration sequence during hold.
} ReflowFn;

/// @brief A single stage within a reflow profile — a heap-allocated linked list node.
///
/// Allocated by ReflowProfileLoad(); freed by ReflowProfileFree(). Do not
/// free individual nodes directly.
typedef struct ReflowStage {
    char                name[ kStageNameLen + 1 ]; ///< Human-readable stage label; empty string = no ty seen.
    Temperature         targetTemp;    ///< Target temperature in milli-°C.
    RampRate            rampRate;      ///< Ramp rate in milli-°C/s. Positive = heat, negative = cool.
    DurationMs          timeoutMs;     ///< Maximum ms to wait for target temperature. 0 = no timeout.
    DurationMs          holdMs;        ///< Milliseconds to hold once target temperature is reached.
    Permille            fanSpeed;      ///< Target oven fan speed for this stage (0–1000).
    DurationMs          accelTimeMs;   ///< Time to ramp from the previous fan speed to fanSpeed. 0 = instant.
    bool                heaterTop;     ///< Enable top heating element (tag h0=).
    bool                heaterRear;    ///< Enable rear convection element (tag h1=).
    bool                heaterBottom;  ///< Enable bottom heating element (tag h2=).
    ReflowFn            function;      ///< Action invoked once at hold entry, before the hold timer starts. ReflowFnNone for no action.
    struct ReflowStage* next;          ///< Next stage in profile, or NULL if last.
} ReflowStage, *ReflowStagePtr;

/// @brief A complete reflow profile — a heap-allocated linked list of stages.
///
/// Allocated by ReflowProfileLoad(); freed by ReflowProfileFree().
typedef struct ReflowProfile {
    ReflowStagePtr stages; ///< Head of stage linked list; NULL = empty.
} ReflowProfile, *ReflowProfilePtr;

// ============================================================================
// PUBLIC API
// ============================================================================

/// @brief Load a profile by name, or build the built-in default if not found.
///
/// Allocates the profile struct and all stage nodes from the heap. Falls back
/// to the built-in lead-free default if @p name is NULL or the file is absent.
/// The caller owns the result and must call ReflowProfileFree() when done.
///
/// @param[in] name  Profile filename or full path. NULL → built-in default.
/// @return Heap-allocated profile, or NULL on allocation failure.
ReflowProfilePtr ReflowProfileLoad( const char* name );

/// @brief Free a heap-allocated profile and all its stage nodes.
///
/// Safe to call with NULL. After returning, the pointer is invalid.
///
/// @param[in] profile  Profile returned by ReflowProfileLoad().
void ReflowProfileFree( ReflowProfilePtr profile );

/// @brief Save a profile to the filesystem.
///
/// @p name may be a bare filename or a full pathname. Bare filenames are
/// written to the profile storage directory.
///
/// @param[in] profile  Profile to serialise.
/// @param[in] name     Destination filename or full path.
/// @return True if the profile was written successfully.
bool ReflowProfileSave( ReflowProfilePtr profile, const char* name );

#endif // REFLOW_PROFILE_H
