/// @file Triac.c
///
/// @brief Phase-angle and burst-fire TRIAC driver implementation.
///
/// Per-instance event flag groups are stored directly in the TriacDevice struct
/// (dev->flags) and are not exposed as extern handles. TriacInitModule() creates
/// them via osEventFlagsNew() and also sets FlagTriacReady in DeviceStatusFlagsHandle.
///
/// The ZCDHandler ISR (fired on AC zero-cross) runs the burst sequencer and arms
/// TIM16 for the first pending gate pulse. The TIM16 ISR (TriacTimerCallback) advances
/// through the sorted firing sequence using a two-phase state (initiation → termination).
///
/// All gate GPIO writes (set/clear) occur only in ZCDHandler and TriacTimerCallback.
/// Task-level functions (TriacOn/Off/Run) update flags and, for TriacOn/Off, also
/// write the GPIO directly since they bypass the sequencer.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "main.h"
#include "Triac.h"

/// @brief TIM16 handle defined in tim.c; used for phase-angle gate pulses.
extern TIM_HandleTypeDef htim16;

/// @brief Internal state for a single TRIAC channel.
typedef struct TriacDevice {
    const TriacID       id;            ///< Channel identifier (const; set at init)
    const uint16_t      pin;           ///< GPIO pin for the gate drive (active-low)
    GPIO_TypeDef* const port;          ///< GPIO port for the gate drive
    osEventFlagsId_t    flags;         ///< Private event flag group for this instance
    TriacDriveParams    currentParams; ///< Currently applied drive parameters
    uint8_t             burstCounter;  ///< Half-cycle counter within the burst window
} TriacDevice, *TriacDevicePtr;

/// @brief Device array with hardware constants set at compile time via designated initialisers.
///
/// Using designated initialisers here ensures that hardware constants (pin, port) are
/// bound to the correct TriacID regardless of array element order.
static TriacDevice devices[ TriacCount ] = {
    [ TriacHeaterTop ]    = { .id = TriacHeaterTop,    .pin = HTR_TOP_EN_N_Pin,  .port = HTR_TOP_EN_N_GPIO_Port  },
    [ TriacHeaterRear ]   = { .id = TriacHeaterRear,   .pin = HTR_REAR_EN_N_Pin, .port = HTR_REAR_EN_N_GPIO_Port },
    [ TriacHeaterBottom ] = { .id = TriacHeaterBottom, .pin = HTR_BOT_EN_N_Pin,  .port = HTR_BOT_EN_N_GPIO_Port  },
    [ TriacOvenFan ]      = { .id = TriacOvenFan,      .pin = OVEN_FAN_EN_N_Pin, .port = OVEN_FAN_EN_N_GPIO_Port },
    [ TriacLight ]        = { .id = TriacLight,        .pin = LIGHT_EN_N_Pin,    .port = LIGHT_EN_N_GPIO_Port    }
};

/// @brief Sorted list of device indices awaiting phase-angle pulses in the current half-cycle.
static uint8_t  activeSequence[ TriacCount ];

/// @brief Number of devices currently in activeSequence.
static uint8_t  sequenceCount = 0;

/// @brief Index of the device currently being serviced by the TIM16 ISR.
static uint8_t  currentSeqIndex = 0;

/// @brief True while TIM16 is timing the 100 µs gate pulse; false while timing the inter-channel gap.
static bool     isPulseWidthDelay = false;

/// @brief HAL tick of the most recent ZCD rising edge, used for the AC-loss watchdog.
static uint32_t lastZcdTick = 0;

static void TriacTimerCallback( TIM_HandleTypeDef* htim );

/// @brief Initialise all TRIAC channels, create per-instance event flags, and register TIM16 callback.
///
/// Creates one osEventFlagsId_t per channel (stored in dev->flags).
/// Sets all gate GPIOs to the safe inactive state (HIGH = gate off, active-low).
/// Sets FlagTriacStatusReady on each channel and signals FlagTriacReady in DeviceStatusFlagsHandle.
void TriacInitModule( void ) {
    for ( int i = 0; i < TriacCount; i++ ) {
        devices[ i ].flags = osEventFlagsNew( NULL );
        // TRIAC gates are Active-Low; SET = Off (gate not conducting)
        HAL_GPIO_WritePin( devices[ i ].port, devices[ i ].pin, GPIO_PIN_SET );
        osEventFlagsSet( devices[ i ].flags, 1 << FlagTriacStatusReady );
    }

    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagTriacReady ) );
    HAL_TIM_RegisterCallback( &htim16, HAL_TIM_PERIOD_ELAPSED_CB_ID, TriacTimerCallback );
}

/// @brief Return a handle to the specified TRIAC channel.
TriacRef TriacOpen( TriacID id ) {
    if ( id >= TriacCount ) return NULL;
    return &devices[ id ];
}

/// @brief Drive a TRIAC at full power, bypassing phase-angle sequencing.
///
/// Clears FlagTriacStatusPhaseAngle, asserts the gate GPIO (LOW), and sets
/// FlagTriacStatusActive and FlagTriacStatusGateOpen.
void TriacOn( TriacRef triac ) {
    if ( !triac ) return;
    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusPhaseAngle );
    HAL_GPIO_WritePin( triac->port, triac->pin, GPIO_PIN_RESET );
    osEventFlagsSet( triac->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
}

/// @brief De-energise a TRIAC and remove it from sequenced control.
///
/// Clears FlagTriacStatusPhaseAngle, de-asserts the gate GPIO (HIGH), and
/// clears FlagTriacStatusActive and FlagTriacStatusGateOpen.
void TriacOff( TriacRef triac ) {
    if ( !triac ) return;
    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusPhaseAngle );
    HAL_GPIO_WritePin( triac->port, triac->pin, GPIO_PIN_SET );
    osEventFlagsClear( triac->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
}

/// @brief Configure a TRIAC for phase-angle / burst-fire control.
///
/// Validates parameters, then loads them and sets FlagTriacStatusPhaseAngle.
/// The change takes effect at the next ZCD interrupt.
void TriacRun( TriacRef triac, TriacDriveParams params ) {
    if ( !triac ) return;

    // Safety check: 10 ms is the absolute limit for a 50 Hz half-cycle
    if ( params.phaseDelayUs > 10000 || params.burstOn > params.burstWindow ) {
        osEventFlagsSet( triac->flags, 1 << FlagTriacStatusConfigError );
        osEventFlagsSet( FaultFlagsHandle, 1 << FlagTriacFault );
        return;
    }

    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusConfigError );
    memcpy( &triac->currentParams, &params, sizeof( TriacDriveParams ) );
    triac->burstCounter = 0;
    osEventFlagsSet( triac->flags, 1 << FlagTriacStatusPhaseAngle );
}

/// @brief Return the full status bitmask for a TRIAC channel.
/// @param[in] triac Handle returned by TriacOpen().
/// @return Bitmask of TriacStatusBit flags; 0 if @p triac is NULL.
/// @note Returns cached event flags — safe to call from any task context.
uint32_t TriacGetStatus( TriacRef triac ) {
    if ( !triac ) return 0;
    return osEventFlagsGet( triac->flags );
}

/// @brief Zero-cross ISR handler — drives the burst sequencer and arms TIM16.
///
/// On every AC zero-cross rising edge:
///   1. Resets the sequencer state variables.
///   2. Iterates all channels: clears transient flags, advances burst counters,
///      and for channels in phase-angle mode, either fires immediately (alpha=0)
///      or adds them to activeSequence for timed firing.
///   3. Insertion-sorts activeSequence by phaseDelayUs (shortest delay first).
///   4. Arms TIM16 with the first channel's phase delay and generates an immediate
///      update event to latch the ARR before starting the timer.
///
/// @warning ISR context — no FreeRTOS blocking APIs. Sets flags and writes GPIO only.
void ZCDHandler( uint16_t GPIO_Pin ) {
    sequenceCount = 0;
    currentSeqIndex = 0;
    isPulseWidthDelay = false;
    lastZcdTick = osKernelGetTickCount();

    for ( int i = 0; i < TriacCount; i++ ) {
        TriacDevice* dev = &devices[ i ];
        uint32_t flags = osEventFlagsGet( dev->flags );

        // Clear transient sequencer flags at the start of every half-cycle
        osEventFlagsClear( dev->flags, ( 1 << FlagTriacStatusZCDLost ) |
                                       ( 1 << FlagTriacStatusPulsePending ) |
                                       ( 1 << FlagTriacStatusPulseActive ) );

        // Skip devices not under sequenced phase-angle control
        if ( !( flags & ( 1 << FlagTriacStatusPhaseAngle ) ) ) continue;

        if ( dev->burstCounter < dev->currentParams.burstOn ) {
            osEventFlagsSet( dev->flags, 1 << FlagTriacStatusActive );

            if ( dev->currentParams.phaseDelayUs == 0 ) {
                // Alpha = 0: fire immediately to avoid jitter from interrupt latency
                HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_RESET );
                osEventFlagsSet( dev->flags, 1 << FlagTriacStatusGateOpen );
            } else {
                // Schedule for timed pulse via TIM16
                osEventFlagsSet( dev->flags, 1 << FlagTriacStatusPulsePending );
                activeSequence[ sequenceCount++ ] = i;
            }
        } else {
            // Burst window 'Off': gate must be high and active flags cleared
            HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_SET );
            osEventFlagsClear( dev->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
        }

        if ( ++dev->burstCounter >= dev->currentParams.burstWindow ) {
            dev->burstCounter = 0;
        }
    }

    // Insertion sort: earliest phase-angle first
    for ( int i = 1; i < sequenceCount; i++ ) {
        uint8_t val = activeSequence[ i ];
        int j = i - 1;
        while ( j >= 0 && devices[ activeSequence[ j ] ].currentParams.phaseDelayUs >
                          devices[ val ].currentParams.phaseDelayUs ) {
            activeSequence[ j + 1 ] = activeSequence[ j ];
            j--;
        }
        activeSequence[ j + 1 ] = val;
    }

    if ( sequenceCount > 0 ) {
        __HAL_TIM_SET_AUTORELOAD( &htim16, devices[ activeSequence[ 0 ] ].currentParams.phaseDelayUs );
        HAL_TIM_GenerateEvent( &htim16, TIM_EVENTSOURCE_UPDATE ); // Latches the new ARR immediately
        HAL_TIM_Base_Start_IT( &htim16 );
    } else {
        HAL_TIM_Base_Stop_IT( &htim16 );
    }
}

/// @brief TIM16 period-elapsed ISR — manages the 100 µs gate pulse and inter-channel gaps.
///
/// Two-phase operation per channel:
///   Phase 1 (isPulseWidthDelay = false): asserts the gate GPIO, marks PulseActive,
///   then reconfigures TIM16 to expire after 100 µs.
///   Phase 2 (isPulseWidthDelay = true): de-asserts any gates still open by the sequencer,
///   advances currentSeqIndex, and either arms TIM16 for the next channel's gap or stops it.
///
/// @param[in] htim TIM handle (used to modify ARR for the next interval).
/// @warning ISR context — no FreeRTOS blocking APIs. Sets flags and writes GPIO only.
static void TriacTimerCallback( TIM_HandleTypeDef* htim ) {
    // Phase 1: Pulse Initiation
    if ( !isPulseWidthDelay ) {
        TriacDevicePtr dev = &devices[ activeSequence[ currentSeqIndex ] ];

        HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_RESET );
        // PulseActive prevents accidental TriacOff during the pulse window
        osEventFlagsSet( dev->flags, ( 1 << FlagTriacStatusGateOpen ) | ( 1 << FlagTriacStatusPulseActive ) );

        // Arm timer for the minimum gate trigger pulse duration (100 µs)
        isPulseWidthDelay = true;
        __HAL_TIM_SET_AUTORELOAD( htim, 100 );
    }
    // Phase 2: Pulse Termination & Interval Calculation
    else {
        // De-assert all pins that were opened by this sequencer session
        for ( int i = 0; i < TriacCount; i++ ) {
            if ( osEventFlagsGet( devices[ i ].flags ) & ( 1 << FlagTriacStatusPulseActive ) ) {
                HAL_GPIO_WritePin( devices[ i ].port, devices[ i ].pin, GPIO_PIN_SET );
                osEventFlagsClear( devices[ i ].flags, ( 1 << FlagTriacStatusGateOpen ) |
                                                       ( 1 << FlagTriacStatusPulseActive ) |
                                                       ( 1 << FlagTriacStatusPulsePending ) );
            }
        }

        isPulseWidthDelay = false;
        currentSeqIndex++;

        if ( currentSeqIndex < sequenceCount ) {
            uint16_t nextDelay = devices[ activeSequence[ currentSeqIndex ] ].currentParams.phaseDelayUs;
            // 'spent' = time already elapsed since ZCD (previous delay + 100 µs pulse)
            uint16_t spent = devices[ activeSequence[ currentSeqIndex - 1 ] ].currentParams.phaseDelayUs + 100;
            // Prevent negative loads if two channels have overlapping delays
            uint16_t load = ( nextDelay > spent ) ? ( nextDelay - spent ) : 1;
            __HAL_TIM_SET_AUTORELOAD( htim, load );
        } else {
            HAL_TIM_Base_Stop_IT( htim );
        }
    }
}

/// @brief Task-loop tick: check the ZCD watchdog and raise faults on AC loss.
///
/// If more than 50 ms have elapsed since the last ZCD interrupt (equivalent to
/// 5 missed zero-crosses at 50 Hz), sets FlagTriacStatusZCDLost on all channels
/// and raises FlagTriacFault in FaultFlagsHandle.
///
/// @warning Must be called from task context.
void TriacProcess( void ) {
    if ( ( osKernelGetTickCount() - lastZcdTick ) > 50 ) {
        for ( int i = 0; i < TriacCount; i++ ) {
            osEventFlagsSet( devices[ i ].flags, 1 << FlagTriacStatusZCDLost );
        }
        osEventFlagsSet( FaultFlagsHandle, 1 << FlagTriacFault );
    }
}
