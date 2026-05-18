/// @file Reflow.h
///
/// @brief Reflow profile engine — public API.
///
/// Manages execution of a named reflow profile. The engine sits above
/// OvenController: it resolves the profile by name via the ReflowProfile
/// module, owns the active stage state machine, and calls `OCSetTarget()`
/// to drive the thermal loop. It does not touch hardware directly.
///
/// Profiles are identified by name or full pathname. `ReflowStart()` loads
/// the profile internally, falling back to the hardcoded default if the
/// named file is not found on the filesystem.
///
/// `ReflowStatusFlagsHandle` is created in `ReflowOpen()`, not by CubeMX.
/// Any task may wait on or read these flags.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef REFLOW_H
#define REFLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"

// ============================================================================
// STATUS FLAGS
// ============================================================================

/// @brief Reflow engine status flags, stored in ReflowStatusFlagsHandle.
///
/// Cleared at the start of each `ReflowStart()` call; set progressively as
/// the cycle executes. `FlagReflowDone` and `FlagReflowAborted` are mutually
/// exclusive terminal states. `FlagReflowPaused` may be set concurrently with
/// any active stage flag.
typedef enum {
    FlagReflowReady = 0,   ///< Engine opened and ready to accept a profile.
    FlagReflowInProgress,  ///< A reflow cycle is actively running.
    FlagReflowPaused,      ///< Cycle suspended; oven holding at current stage target.
    FlagReflowPreheating,  ///< Currently in the preheat ramp stage.
    FlagReflowSoaking,     ///< Currently in the thermal soak stage.
    FlagReflowReflowing,   ///< Currently in the reflow (liquidus) stage.
    FlagReflowCooling,     ///< Currently in the cooldown stage.
    FlagReflowDone,         ///< Cycle completed successfully.
    FlagReflowAborted,      ///< Cycle was stopped before completion.
    FlagReflowStatusFault,  ///< Fault during execution — also propagated to FaultFlagsHandle.

    ReflowFlagsCount       ///< Number of flags — must stay <= 24.
} ReflowFlagBit;

/// @brief Reflow engine status event flag group.
///
/// Created by `ReflowOpen()`. Any task may wait on or read these flags.
extern osEventFlagsId_t ReflowStatusFlagsHandle;

// ============================================================================
// HANDLE
// ============================================================================

/// @brief Opaque handle to the reflow engine instance.
typedef struct ReflowInstance* ReflowRef;

// ============================================================================
// PUBLIC API
// ============================================================================

/// @brief Open the reflow engine and acquire its OvenController reference.
///
/// Creates `ReflowStatusFlagsHandle` and initialises the engine state.
/// Must be called once before any other Reflow function.
///
/// @return Handle to the engine instance, or NULL on failure.
ReflowRef ReflowOpen( void );

/// @brief Load a profile by name and begin execution.
///
/// Resolves @p name via the ReflowProfile module. If the named file is not
/// found on the filesystem, the hardcoded default profile is used. Resets
/// all status flags and starts the stage state machine from stage 0.
///
/// @param[in] ref   Engine handle returned by `ReflowOpen()`.
/// @param[in] name  Profile filename or full path. Pass NULL to use the default.
/// @return True if the profile was accepted and execution has started.
bool ReflowStart( ReflowRef ref, const char* name );

/// @brief Abort the active cycle and return the oven to idle.
///
/// Sets `FlagReflowAborted`, calls `OCStop()`, and clears `FlagReflowInProgress`.
/// Safe to call at any point, including when no cycle is running.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
void ReflowAbort( ReflowRef ref );

/// @brief Suspend the active cycle and hold at the current stage target temperature.
///
/// Sets `FlagReflowPaused`. The stage timer is frozen and the OvenController
/// continues to regulate at the current target. Has no effect if no cycle is
/// running or the cycle is already paused.
///
/// @note Pausing during the reflow stage risks the board cooling below liquidus.
///       The caller is responsible for any decision to pause at that point.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
void ReflowPause( ReflowRef ref );

/// @brief Resume a paused cycle from the point at which it was suspended.
///
/// Clears `FlagReflowPaused` and restarts the stage timer. Has no effect if
/// the cycle is not paused.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
void ReflowResume( ReflowRef ref );

/// @brief Execute one iteration of the reflow state machine.
///
/// Called from `ReflowTaskLoop()` in a continuous loop. When a cycle is
/// active this function may block on `osDelay()` during timed holds. When
/// idle it blocks on `FlagReflowInProgress` to yield the CPU.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
void ReflowProcess( ReflowRef ref );

/// @brief Return the current status flags as a snapshot bitmask.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
/// @return Bitmask of `ReflowFlagBit` values currently set.
uint32_t ReflowGetStatus( ReflowRef ref );

/// @brief Return the zero-based index of the stage currently executing.
///
/// Valid only while `FlagReflowInProgress` is set. Returns 0 when idle.
/// Combined with the profile name this identifies the exact position in
/// a multi-segment cycle.
///
/// @param[in] ref  Engine handle returned by `ReflowOpen()`.
/// @return Current stage index (0 to stageCount − 1).
uint8_t ReflowGetStageIndex( ReflowRef ref );

#endif // REFLOW_H
