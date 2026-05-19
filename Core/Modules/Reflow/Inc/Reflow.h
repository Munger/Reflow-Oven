/// @file Reflow.h
///
/// @brief Reflow profile engine — public API.
///
/// Singleton module. ReflowInitModule() allocates state and signals readiness.
/// ManagerTask starts a cycle by calling ReflowStart(), then sets FlagReflowStop
/// to end it. ReflowProcess() runs the stage state machine from ReflowTask.
///
/// All reflow status and command bits live in ReflowFlagsHandle, created by
/// CubeMX. Any task may wait on or read these flags directly.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef REFLOW_H
#define REFLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "SystemStatusFlags.h"

// ============================================================================
// FLAGS
// ============================================================================

/// @brief Event flag group for reflow command and status bits. Created by CubeMX.
extern osEventFlagsId_t ReflowFlagsHandle;

/// @brief Reflow command and status flag bit indices for use with BIT().
///
/// Command bits are set by ManagerTask and cleared by ReflowProcess() after
/// acting on them. Status bits are set and cleared exclusively by ReflowProcess().
/// FlagReflowRunning remains set during pause. FlagReflowDone is the only
/// explicit terminal state; absence of both signals a stopped or faulted cycle
/// (see FaultFlagsHandle for fault detail).
typedef enum {
    // Command bits — written by ManagerTask
    FlagReflowStart = 0, ///< Start a cycle; profile already loaded into state by ReflowStart().
    FlagReflowStop,      ///< Stop the active cycle.

    // Status bits — written by ReflowProcess()
    FlagReflowRunning,   ///< A reflow cycle is active.
    FlagReflowHoldActive,///< Oven has reached stage target; hold condition is being evaluated.
    FlagReflowDone,      ///< Cycle completed successfully.

    ReflowFlagsCount     ///< Number of flag bits — must stay <= 32.
} ReflowFlagBit;

_Static_assert( ReflowFlagsCount <= 32, "Reflow flags out of bounds" );

// ============================================================================
// PUBLIC API
// ============================================================================

/// @brief Initialise singleton state and signal module readiness.
///
/// Called by ManagerTask during the InitModule phase. Acquires OvenController
/// and ACFan references and sets FlagReflowEngineReady in DeviceStatusFlagsHandle.
void ReflowInitModule( void );

/// @brief Load a profile by name and begin execution.
///
/// Resolves @p name via the ReflowProfile module, falling back to the hardcoded
/// default if the named file is not found. Starts the OvenController and sets
/// FlagReflowInProgress. Clears all transient status flags before starting.
///
/// @param[in] name  Profile filename or full path. Pass NULL to use the default.
/// @return True if the profile was accepted and execution has started.
bool ReflowStart( const char* name );

/// @brief Execute one iteration of the reflow state machine.
///
/// Called from ReflowTask in a continuous loop. Blocks on FlagReflowInProgress
/// when idle. When a cycle is active, advances the stage machine, checks hold
/// conditions, and watches FlagReflowStop and FlagSystemAborted between ticks.
void ReflowProcess( void );

#endif // REFLOW_H
