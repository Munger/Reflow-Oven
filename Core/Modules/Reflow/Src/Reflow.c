/// @file Reflow.c
///
/// @brief Reflow profile engine — state machine implementation.
///
/// Sits above OvenController and ACFan. ReflowOpen() acquires refs to both
/// (idempotent — ManagerTask opens them first). ReflowStart() loads a named
/// profile via ReflowProfile, configures the OvenControlPB, and calls
/// OCStart(). ReflowProcess() advances the stage state machine each tick,
/// updating fan speed and checking hold conditions. OCProcess() in DeviceTask
/// handles the thermal regulation loop independently.
///
/// Stage flags (FlagReflowPreheating, etc.) follow the ReflowStageType of the
/// active stage. FlagReflowPaused may be set concurrently with any stage flag;
/// it freezes stage and hold timers but OvenController continues to regulate.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include "Reflow.h"
#include "ReflowProfile.h"
#include "OvenController.h"
#include "ACFan.h"

// ============================================================================
// Constants
// ============================================================================

/// @brief Oven temperature tolerance in milli-°C used for all stages.
static const Temperature kStageTolerance = 3000;  // ±3 °C

// ============================================================================
// Instance state
// ============================================================================

/// @brief Internal state of the reflow engine (singleton).
typedef struct ReflowInstance {
    OvenControllerRef controller;  ///< Opened in ReflowOpen(); held for lifetime of engine
#if FEATURE_OVEN_FAN
    ACFanRef          fan;         ///< Opened in ReflowOpen(); NULL if calibration not available
#endif // FEATURE_OVEN_FAN
    OvenControlPB     pb;          ///< Owned by this instance; passed to OCStart()
    ReflowProfile     profile;     ///< Active profile; copied from ReflowProfileLoad or default
    uint8_t           stageIndex;  ///< Zero-based index of the stage currently executing
    uint32_t          holdStartMs; ///< Kernel tick at which the hold condition first became true
    bool              holdActive;  ///< True once the oven has reached target and hold has begun
    uint32_t          stageStartMs;///< Kernel tick at which the current stage was applied
} ReflowInstance, *ReflowInstancePtr;

/// @brief Public handle to the reflow engine status flags.
osEventFlagsId_t ReflowStatusFlagsHandle;

/// @brief Singleton engine instance.
static ReflowInstance s_instance;

// ============================================================================
// Internal helpers
// ============================================================================

/// @brief Map a ReflowStageType to the corresponding status flag bit.
static uint32_t StageFlagBit( ReflowStageType type ) {
    switch ( type ) {
        case ReflowStagePreheat: return BIT( FlagReflowPreheating );
        case ReflowStageSoak:    return BIT( FlagReflowSoaking    );
        case ReflowStageReflow:  return BIT( FlagReflowReflowing  );
        case ReflowStageCool:    return BIT( FlagReflowCooling    );
        default:                 return 0;
    }
}

/// @brief Bitmask of all stage-phase flags; used to clear before setting the new one.
static const uint32_t kAllStageBits =
    BIT( FlagReflowPreheating ) |
    BIT( FlagReflowSoaking    ) |
    BIT( FlagReflowReflowing  ) |
    BIT( FlagReflowCooling    );

/// @brief Load stage @p idx into the OvenControlPB and initialise stage tracking.
///
/// Updates pb mandate fields from the stage definition, sets the fan to fanStart,
/// clears the hold state, and records the current tick as the stage-start time.
/// The OvenController picks up the updated PB on its next OCProcess() tick —
/// no re-call to OCStart() is needed between stages; we simply update the PB
/// in place (task-safe because OvenController reads mandate fields directly
/// through the pointer each tick).
static void ApplyStage( ReflowInstancePtr inst, uint8_t idx ) {
    const ReflowStage* stage = &inst->profile.stages[ idx ];

    inst->stageIndex   = idx;
    inst->holdActive   = false;
    inst->stageStartMs = osKernelGetTickCount();

    // Update OvenController mandate via the shared PB
    inst->pb.targetTemp = (Temperature)stage->targetTempC * 1000;
    inst->pb.rampRate   = ( stage->rampRateC > 0.0f )
                          ? (Temperature)( stage->rampRateC * 1000.0f )
                          : 0;   // cooling: no ramp limit; OC waits passively
    inst->pb.tolerance  = kStageTolerance;
    inst->pb.heaters    = stage->heaters;

#if FEATURE_OVEN_FAN
    if ( inst->fan ) {
        ACFanSetSpeed( inst->fan, stage->fanStart );
    }
#endif // FEATURE_OVEN_FAN

    // Set the new stage flag, clearing all previous stage flags
    uint32_t newBit = StageFlagBit( stage->type );
    osEventFlagsClear( ReflowStatusFlagsHandle, kAllStageBits & ~newBit );
    if ( newBit ) {
        osEventFlagsSet( ReflowStatusFlagsHandle, newBit );
    }
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Open the reflow engine and acquire OvenController and ACFan references.
///
/// Idempotent — subsequent calls return the existing handle without re-initialising.
/// Sets FlagReflowReady and FlagReflowEngineReady in DeviceStatusFlagsHandle.
ReflowRef ReflowOpen( void ) {
    if ( ReflowStatusFlagsHandle != NULL ) {
        return &s_instance;
    }

    memset( &s_instance, 0, sizeof( s_instance ) );

    ReflowStatusFlagsHandle = osEventFlagsNew( NULL );

    s_instance.controller = OCOpen( OvenController1 );
#if FEATURE_OVEN_FAN
    s_instance.fan = ACFanOpen( OvenFan, TriacOvenFan, RotaryEncoder1 );
#endif // FEATURE_OVEN_FAN

    osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowReady ) );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagReflowEngineReady ) );

    return &s_instance;
}

/// @brief Load a profile by name and start execution from stage 0.
///
/// If @p name is NULL or the file is not found on the filesystem, the hardcoded
/// default profile is used. Resets all transient status flags and calls OCStart().
bool ReflowStart( ReflowRef ref, const char* name ) {
    if ( !ref ) return false;
    ReflowInstancePtr inst = (ReflowInstancePtr)ref;

    if ( !inst->controller ) return false;

    // Load profile — fall back to default if unavailable
    ReflowProfile loaded;
    if ( name && ReflowProfileLoad( name, &loaded ) ) {
        memcpy( &inst->profile, &loaded, sizeof( ReflowProfile ) );
    } else {
        memcpy( &inst->profile, ReflowProfileGetDefault(), sizeof( ReflowProfile ) );
    }

    if ( inst->profile.stageCount == 0 ||
         inst->profile.stageCount > kReflowMaxStages ) {
        return false;
    }

    // Clear all transient cycle flags
    osEventFlagsClear( ReflowStatusFlagsHandle,
        BIT( FlagReflowDone    ) | BIT( FlagReflowAborted ) |
        BIT( FlagReflowPaused  ) | BIT( FlagReflowFault   ) |
        kAllStageBits );

    // Initialise the control block — OCStart() seeds rampTarget from sensors
    memset( &inst->pb, 0, sizeof( OvenControlPB ) );

    // Apply stage 0 and start the controller
    ApplyStage( inst, 0 );
    OCStart( inst->controller, &inst->pb );

    osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowInProgress ) );
    return true;
}

/// @brief Abort the active cycle and de-energise the oven.
void ReflowAbort( ReflowRef ref ) {
    if ( !ref ) return;
    ReflowInstancePtr inst = (ReflowInstancePtr)ref;

    OCStop( inst->controller );
#if FEATURE_OVEN_FAN
    if ( inst->fan ) ACFanSetSpeed( inst->fan, 0 );
#endif // FEATURE_OVEN_FAN

    osEventFlagsClear( ReflowStatusFlagsHandle,
        BIT( FlagReflowInProgress ) | BIT( FlagReflowPaused ) | kAllStageBits );
    osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowAborted ) );
}

/// @brief Suspend the cycle. OvenController continues to regulate at the current target.
void ReflowPause( ReflowRef ref ) {
    if ( !ref ) return;
    uint32_t flags = osEventFlagsGet( ReflowStatusFlagsHandle );
    if ( !( flags & BIT( FlagReflowInProgress ) ) ) return;
    if (    flags & BIT( FlagReflowPaused      ) ) return;
    osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowPaused ) );
}

/// @brief Resume a paused cycle; stage and hold timers restart from now.
void ReflowResume( ReflowRef ref ) {
    if ( !ref ) return;
    uint32_t flags = osEventFlagsGet( ReflowStatusFlagsHandle );
    if ( !( flags & BIT( FlagReflowPaused ) ) ) return;

    ReflowInstancePtr inst = (ReflowInstancePtr)ref;

    // Reset timers so pause time is not counted against the hold budget
    uint32_t now    = osKernelGetTickCount();
    inst->stageStartMs = now;
    if ( inst->holdActive ) {
        inst->holdStartMs = now;
    }

    osEventFlagsClear( ReflowStatusFlagsHandle, BIT( FlagReflowPaused ) );
}

/// @brief Return the current status flags snapshot.
uint32_t ReflowGetStatus( ReflowRef ref ) {
    if ( !ref ) return 0;
    return osEventFlagsGet( ReflowStatusFlagsHandle );
}

/// @brief Return the zero-based index of the stage currently executing.
uint8_t ReflowGetStageIndex( ReflowRef ref ) {
    if ( !ref ) return 0;
    return ( (ReflowInstancePtr)ref )->stageIndex;
}

/// @brief Advance the reflow state machine by one iteration.
///
/// When idle, blocks on FlagReflowInProgress to yield the CPU.
/// When running, checks hold conditions, updates fan speed, and advances stages.
/// Delays 200 ms per tick so the OC regulation loop (in DeviceTask) runs freely.
void ReflowProcess( ReflowRef ref ) {
    if ( !ref ) { osDelay( 100 ); return; }
    ReflowInstancePtr inst = (ReflowInstancePtr)ref;

    uint32_t flags = osEventFlagsGet( ReflowStatusFlagsHandle );

    // If idle, block until a cycle is started
    if ( !( flags & BIT( FlagReflowInProgress ) ) ) {
        osEventFlagsWait( ReflowStatusFlagsHandle, BIT( FlagReflowInProgress ),
                          osFlagsWaitAll | osFlagsNoClear, osWaitForever );
        return;
    }

    // If paused, yield without advancing
    if ( flags & BIT( FlagReflowPaused ) ) {
        osDelay( 50 );
        return;
    }

    // Propagate OvenController fault to Reflow
    uint32_t ocStatus = OCGetStatus( inst->controller );
    if ( ocStatus & BIT( FlagOvenControllerStatusFault ) ) {
        ReflowAbort( ref );
        osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowStatusFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagReflowFault ) );
        return;
    }

    const ReflowStage* stage = &inst->profile.stages[ inst->stageIndex ];

    // Start hold timer the first time the oven reaches the stage target
    if ( !inst->holdActive && ( ocStatus & BIT( FlagOvenControllerStatusAtTemp ) ) ) {
        inst->holdActive  = true;
        inst->holdStartMs = osKernelGetTickCount();
    }

#if FEATURE_OVEN_FAN
    // Interpolate fan speed during hold when fanStart ≠ fanEnd
    if ( inst->fan && inst->holdActive && ( stage->fanStart != stage->fanEnd ) ) {
        if ( stage->holdCriterion == ReflowHoldTime && stage->holdMs > 0 ) {
            uint32_t elapsed  = osKernelGetTickCount() - inst->holdStartMs;
            Permille fraction = (Permille)( elapsed * 1000UL / stage->holdMs );
            if ( fraction > 1000 ) fraction = 1000;
            Permille speed = (Permille)( (int32_t)stage->fanStart +
                             ( (int32_t)stage->fanEnd - (int32_t)stage->fanStart ) *
                             (int32_t)fraction / 1000 );
            ACFanSetSpeed( inst->fan, speed );
        } else {
            ACFanSetSpeed( inst->fan, stage->fanEnd );
        }
    }
#endif // FEATURE_OVEN_FAN

    // Evaluate hold criterion
    bool holdComplete = false;
    if ( inst->holdActive ) {
        if ( stage->holdCriterion == ReflowHoldTime ) {
            holdComplete = ( osKernelGetTickCount() - inst->holdStartMs ) >= stage->holdMs;
        } else {
            // ReflowHoldTemperature: advance when currentTemp crosses the threshold
            int32_t currentC = (int32_t)( inst->pb.currentTemp / 1000 );
            holdComplete = ( stage->rampRateC >= 0.0f )
                           ? ( currentC >= stage->holdTempC )   // heating: above threshold
                           : ( currentC <= stage->holdTempC );  // cooling: below threshold
        }
    }

    if ( holdComplete ) {
        uint8_t nextIdx = inst->stageIndex + 1;
        if ( nextIdx < inst->profile.stageCount ) {
            ApplyStage( inst, nextIdx );
        } else {
            // All stages complete — stop the controller and signal done
            OCStop( inst->controller );
#if FEATURE_OVEN_FAN
            if ( inst->fan ) ACFanSetSpeed( inst->fan, 0 );
#endif // FEATURE_OVEN_FAN
            osEventFlagsClear( ReflowStatusFlagsHandle,
                BIT( FlagReflowInProgress ) | kAllStageBits );
            osEventFlagsSet( ReflowStatusFlagsHandle, BIT( FlagReflowDone ) );
        }
    }

    osDelay( 200 );
}
