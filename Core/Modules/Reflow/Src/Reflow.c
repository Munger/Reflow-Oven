/// @file Reflow.c
///
/// @brief Reflow profile engine — singleton state machine implementation.
///
/// Sits above OvenController and ACFan. ReflowInitModule() acquires refs to
/// both at startup. ReflowStart() loads a named profile, configures the
/// OvenControlPB, and calls OCStart(). ReflowProcess() advances the stage
/// state machine each tick, updating fan speed and checking hold conditions.
/// OCProcess() in DeviceTask handles the thermal regulation loop independently.
///
/// ReflowProcess() watches FlagReflowStop between ticks. On stop, or on
/// OvenController fault, it calls StopCycle() and returns to idle.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include "TaskUtils.h"
#include "Reflow.h"
#include "ReflowProfile.h"
#include "OvenController.h"
#include "ACFan.h"
#if FEATURE_BOARD_FAN
#include "DCFan.h"
#endif // FEATURE_BOARD_FAN

// ============================================================================
// Constants
// ============================================================================

static const Temperature kStageTolerance = 3000;  // ±3 °C in milli-°C

// ============================================================================
// Singleton state
// ============================================================================

typedef struct {
    OvenControllerRef controller;
#if FEATURE_OVEN_FAN
    ACFanRef          fan;
    Permille          fanSpeed;     ///< Last speed set on the fan; used as the ramp start for the next stage.
    Permille          fanFrom;      ///< Fan speed at the start of the current stage; ramp origin.
#endif // FEATURE_OVEN_FAN
#if FEATURE_BOARD_FAN
    DCFanRef          boardFan;
#endif // FEATURE_BOARD_FAN
    OvenControlPB     pb;
    ReflowProfilePtr  profile;      ///< Heap-allocated; NULL when idle.
    ReflowStagePtr    currentStage; ///< Pointer into profile's linked list.
    uint32_t          holdStartMs;
    uint32_t          stageStartMs;
} ReflowState;

static ReflowState state;

// ============================================================================
// Internal helpers
// ============================================================================


/// @brief Set @p stage as the active stage and initialise tracking.
///
/// Updates the OvenControlPB mandate from the stage definition, sets the fan
/// to fanStart, clears FlagReflowHoldActive, and records the current tick as
/// the stage-start time. The OvenController picks up the updated PB on its
/// next OCProcess() tick — no re-call to OCStart() is needed between stages.
static void ApplyStage( ReflowStagePtr stage ) {
    state.currentStage = stage;
    state.stageStartMs = osKernelGetTickCount();
    osEventFlagsClear( ReflowFlagsHandle, BIT( FlagReflowHoldActive ) );

    state.pb.targetTemp   = stage->targetTemp;
    state.pb.rampRate     = ( stage->rampRate > 0 ) ? stage->rampRate : 0;
    state.pb.tolerance    = ( stage->targetTemp > 0 ) ? kStageTolerance : 0;
    state.pb.heaterTop    = stage->heaterTop;
    state.pb.heaterRear   = stage->heaterRear;
    state.pb.heaterBottom = stage->heaterBottom;

#if FEATURE_OVEN_FAN
    state.fanFrom = state.fanSpeed;
#endif // FEATURE_OVEN_FAN

}

/// @brief Stop the active cycle, de-energise the oven, and free the profile.
static void StopCycle( void ) {
    OCStop( state.controller );
#if FEATURE_OVEN_FAN
    if ( state.fan ) ACFanSetSpeed( state.fan, 0 );
    state.fanSpeed = 0;
#endif // FEATURE_OVEN_FAN

    ReflowProfileFree( state.profile );
    state.profile      = NULL;
    state.currentStage = NULL;

    osEventFlagsClear( ReflowFlagsHandle,
        BIT( FlagReflowStop ) | BIT( FlagReflowRunning ) | BIT( FlagReflowHoldActive ) );
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Acquire OvenController and ACFan references and signal module readiness.
void ReflowInitModule( void ) {
    memset( &state, 0, sizeof( state ) );

    state.controller = OCOpen( OvenController1 );

#if FEATURE_OVEN_FAN
    state.fan = ACFanOpen( OvenFan, TriacOvenFan );
#if FEATURE_ROTARY_ENCODER
    ACFanAttachEncoder( state.fan, OvenFanEncoder );
#endif // FEATURE_ROTARY_ENCODER
#endif // FEATURE_OVEN_FAN
#if FEATURE_BOARD_FAN
    state.boardFan = DCFanOpen( BoardCoolingFan, NULL );
#endif // FEATURE_BOARD_FAN

}

/// @brief Load a profile by name and signal ReflowProcess() to begin execution.
///
/// Falls back to the hardcoded default if @p name is NULL or the file is not
/// found. Sets FlagReflowStart; ReflowProcess() picks it up on the next tick
/// and owns the cycle initialisation from there.
bool ReflowStart( const char* name ) {
    if ( !state.controller ) return false;

    ReflowProfileFree( state.profile );
    state.profile = ReflowProfileLoad( name );
    if ( !state.profile || !state.profile->stages ) {
        ReflowProfileFree( state.profile );
        state.profile = NULL;
        return false;
    }

    osEventFlagsSet( ReflowFlagsHandle, BIT( FlagReflowStart ) );
    return true;
}

/// @brief Advance the reflow state machine by one iteration.
///
/// Blocks on FlagReflowRunning when idle. When running, checks hold
/// conditions, updates fan speed, and advances stages. Delays 200 ms per
/// tick so the OC regulation loop runs freely.
void ReflowProcess( void ) {
    uint32_t flags = osEventFlagsGet( ReflowFlagsHandle );

    if ( !( flags & BIT( FlagReflowRunning ) ) ) {
        osEventFlagsWait( ReflowFlagsHandle, BIT( FlagReflowStart ),
                          osFlagsWaitAll | osFlagsNoClear, osWaitForever );

        osEventFlagsClear( ReflowFlagsHandle,
            BIT( FlagReflowStart ) | BIT( FlagReflowDone ) | BIT( FlagReflowHoldActive ) );

        memset( &state.pb, 0, sizeof( OvenControlPB ) );
        ApplyStage( state.profile->stages );
        OCStart( state.controller, &state.pb );
        osEventFlagsSet( ReflowFlagsHandle, BIT( FlagReflowRunning ) );
        return;
    }

    uint32_t ocStatus = OCGetStatus( state.controller );

    const ReflowStage* stage = state.currentStage;

    if ( !( flags & BIT( FlagReflowHoldActive ) ) && stage->timeoutMs > 0 &&
         ( osKernelGetTickCount() - state.stageStartMs ) >= stage->timeoutMs ) {
        StopCycle();
        return;
    }

    if ( !( flags & BIT( FlagReflowHoldActive ) ) &&
         ( stage->targetTemp == 0 || ( ocStatus & BIT( FlagOvenControllerStatusAtTemp ) ) ) ) {
        osEventFlagsSet( ReflowFlagsHandle, BIT( FlagReflowHoldActive ) );
        flags |= BIT( FlagReflowHoldActive );

#if FEATURE_OVEN_FAN && FEATURE_AC_FAN_CALIBRATION
        if ( stage->function == ReflowFnCalFan && state.fan )
            ACFanCalibrate( state.fan );
#endif
#if FEATURE_BOARD_FAN
        if ( stage->function == ReflowFnCalBoardFan && state.boardFan )
            DCFanCalibrate( state.boardFan );
#endif

        state.holdStartMs = osKernelGetTickCount();
    }

#if FEATURE_OVEN_FAN
    if ( state.fan ) {
        Permille speed = stage->fanSpeed;
        if ( stage->accelTimeMs > 0 ) {
            uint32_t elapsed = osKernelGetTickCount() - state.stageStartMs;
            if ( elapsed < stage->accelTimeMs ) {
                Permille frac = (Permille)( elapsed * 1000UL / stage->accelTimeMs );
                speed = (Permille)( (int32_t)state.fanFrom +
                        (int32_t)( (int32_t)stage->fanSpeed - (int32_t)state.fanFrom ) *
                        (int32_t)frac / 1000 );
            }
        }
        ACFanSetSpeed( state.fan, speed / 10 );
        state.fanSpeed = speed;
    }
#endif // FEATURE_OVEN_FAN

    bool holdComplete = ( flags & BIT( FlagReflowHoldActive ) ) &&
                        ( osKernelGetTickCount() - state.holdStartMs ) >= stage->holdMs;

    if ( holdComplete ) {
        ReflowStagePtr next = state.currentStage->next;
        if ( next ) {
            ApplyStage( next );
        } else {
            OCStop( state.controller );
#if FEATURE_OVEN_FAN
            if ( state.fan ) ACFanSetSpeed( state.fan, 0 );
            state.fanSpeed = 0;
#endif // FEATURE_OVEN_FAN
            ReflowProfileFree( state.profile );
            state.profile      = NULL;
            state.currentStage = NULL;
            osEventFlagsClear( ReflowFlagsHandle,
                BIT( FlagReflowRunning ) | BIT( FlagReflowHoldActive ) );
            osEventFlagsSet( ReflowFlagsHandle, BIT( FlagReflowDone ) );
        }
    }

    if ( WaitForFlags( ReflowFlagsHandle, BIT( FlagReflowStop ), 200 ) ) { StopCycle(); }
}
