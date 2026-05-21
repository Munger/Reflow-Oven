/// @file Buzzer.c
///
/// @brief Piezo buzzer driver — TIM7 interrupt-based melodic sequencer.
///
/// All hardware I/O (timer configuration, GPIO writes) lives here. The public
/// BuzzerStart/Stop/Play/PlayMelody functions configure the sequencer state and
/// may write GPIO directly. The TIM7 period-elapsed ISR advances the sequencer
/// and drives the GPIO toggle. BuzzerProcess() is intentionally empty — all
/// real-time control is ISR-driven.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_BUZZER

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "Platform.h"
#include "main.h"
#include "tim.h"
#include "Buzzer.h"

// ============================================================================
// Pre-defined melodies
// ============================================================================

/// @brief Pre-defined melody: 31-note level-complete fanfare.
static const struct {
    uint8_t    length;
    BuzzerTone tones[ 31 ];
} levelCompleteInternal = {
    .length = 31,
    .tones = {
        { NoteG7, 150 }, { NoteC8, 150 }, { NoteE8, 150 }, { NoteG7, 150 }, { NoteC8, 150 }, { NoteE8, 150 },
        { NoteGs7, 150 }, { NoteC8, 150 }, { NoteDs8, 150 }, { NoteGs7, 150 }, { NoteC8, 150 }, { NoteDs8, 150 },
        { NoteAs7, 150 }, { NoteD8, 150 }, { NoteE8, 150 }, { NoteAs7, 150 }, { NoteD8, 150 }, { NoteE8, 150 },
        { NoteC8, 200 }, { NoteRest, 50 }, { NoteC8, 200 }, { NoteRest, 50 }, { NoteC8, 200 }, { NoteRest, 50 },
        { NoteC8, 150 }, { NoteB7, 150 }, { NoteAs7, 150 }, { NoteA7, 150 },
        { NoteGs7, 400 }, { NoteAs7, 400 }, { NoteC8, 1000 }
    }
};

/// @brief Pre-defined melody: power-on ascending arpeggio.
static const struct { uint8_t length; BuzzerTone tones[ 3 ]; } patternPowerOn = {
    .length = 3, .tones = { { NoteC7, 100 }, { NoteE7, 100 }, { NoteG7, 200 } }
};

/// @brief Pre-defined melody: short success chime.
static const struct { uint8_t length; BuzzerTone tones[ 2 ]; } patternSuccess = {
    .length = 2, .tones = { { NoteG7, 100 }, { NoteC8, 300 } }
};

/// @brief Pre-defined melody: error double-beep.
static const struct { uint8_t length; BuzzerTone tones[ 3 ]; } patternError = {
    .length = 3, .tones = { { NoteC7, 200 }, { NoteRest, 50 }, { NoteC7, 400 } }
};

/// @brief Pre-defined melody: rapid high-frequency critical alert.
static const struct { uint8_t length; BuzzerTone tones[ 4 ]; } patternCritical = {
    .length = 4, .tones = { { NoteDs8, 150 }, { NoteRest, 50 }, { NoteDs8, 150 }, { NoteRest, 50 } }
};

/// @brief Lookup table mapping BuzzerPattern enum values to melody pointers.
static const Melody* patterns[] = {
    ( const Melody* )&patternPowerOn,
    ( const Melody* )&patternSuccess,
    ( const Melody* )&patternError,
    ( const Melody* )&patternCritical,
    ( const Melody* )&levelCompleteInternal
};

// ============================================================================
// Instance type
// ============================================================================

/// @brief Internal state for one buzzer instance.
typedef struct BuzzerInstance {
    osEventFlagsId_t       statusHandle;
    StaticEventGroup_t     statusBuffer;    ///< Storage backing statusHandle (no-heap allocation)
    volatile const Melody* currentMelody;    ///< Melody currently being played (written by task, read by ISR)
    volatile uint8_t       currentIndex;     ///< Index of the note currently playing
    volatile uint32_t      remainingToggles; ///< Timer ticks remaining for the current note
} BuzzerInstance, *BuzzerInstancePtr;

/// @brief All buzzer instances — indexed by BuzzerID.
static BuzzerInstance instances[ BuzzerCount ];

static void PrepareNote( BuzzerInstancePtr buzzer );
static void TimerHandler( TIM_HandleTypeDef *htim );

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate per-instance resources and register the TIM7 callback.
void BuzzerInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    for ( uint8_t i = 0; i < BuzzerCount; i++ ) {
        instances[ i ].statusHandle = osEventFlagsNew( &(osEventFlagsAttr_t){ .cb_mem = &instances[ i ].statusBuffer, .cb_size = sizeof( StaticEventGroup_t ) } );
    }
    HAL_TIM_RegisterCallback( &htim7, HAL_TIM_PERIOD_ELAPSED_CB_ID, TimerHandler );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBuzzerReady ) );
}

/// @brief Open a buzzer instance and perform one-time hardware initialisation.
///
/// Idempotent — subsequent calls with the same @p id return the existing handle.
BuzzerRef BuzzerOpen( BuzzerID id ) {
    if ( id >= BuzzerCount ) return NULL;
    BuzzerInstancePtr buzzer = &instances[ id ];

    if ( osEventFlagsGet( buzzer->statusHandle ) & BIT( FlagBuzzerStatusReady ) ) {
        return buzzer;
    }

    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    osEventFlagsSet( buzzer->statusHandle, BIT( FlagBuzzerStatusReady ) );

    return buzzer;
}

/// @brief Start the buzzer at a specific frequency, bypassing the sequencer.
void BuzzerStart( BuzzerRef buzzer, BuzzerFrequency frequency ) {
    if ( buzzer == NULL ) return;
    BuzzerInstancePtr inst = (BuzzerInstancePtr)buzzer;

    if ( frequency == NoteRest ) {
        taskENTER_CRITICAL();
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
        taskEXIT_CRITICAL();
        return;
    }

    uint32_t period = 500000 / ( uint32_t )frequency;
    taskENTER_CRITICAL();
    __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    osEventFlagsSet( inst->statusHandle, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
    taskEXIT_CRITICAL();
}

/// @brief Immediately stop the PWM signal and de-assert the output GPIO.
void BuzzerStop( BuzzerRef buzzer ) {
    if ( buzzer == NULL ) return;
    BuzzerInstancePtr inst = (BuzzerInstancePtr)buzzer;

    taskENTER_CRITICAL();
    HAL_TIM_Base_Stop_IT( &htim7 );
    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    osEventFlagsClear( inst->statusHandle, BIT( FlagBuzzerStatusActive ) );
    taskEXIT_CRITICAL();
}

/// @brief Queue a pre-defined melodic pattern for asynchronous ISR-driven playback.
void BuzzerPlay( BuzzerRef buzzer, BuzzerPattern pattern ) {
    BuzzerPlayMelody( buzzer, patterns[ ( uint8_t )pattern ] );
}

/// @brief Queue a custom melody for asynchronous ISR-driven playback.
///
/// Loads the melody into the sequencer and starts TIM7. The function returns
/// immediately; the ISR advances through notes autonomously.
/// @warning The melody pointer must remain valid for the entire playback duration.
void BuzzerPlayMelody( BuzzerRef buzzer, const Melody* melody ) {
    if ( buzzer == NULL || !melody || melody->length == 0 ) return;
    BuzzerInstancePtr inst = (BuzzerInstancePtr)buzzer;

    taskENTER_CRITICAL();
    inst->currentMelody = melody;
    inst->currentIndex  = 0;
    PrepareNote( inst );
    osEventFlagsSet( inst->statusHandle, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
    taskEXIT_CRITICAL();
}

/// @brief Task-loop tick for the buzzer module (currently a no-op; sequencing is ISR-driven).
void BuzzerProcess( void ) {
}

/// @brief Return the current bitmask from the instance status flags.
uint32_t BuzzerGetStatus( BuzzerRef buzzer ) {
    if ( buzzer == NULL ) return 0;
    return osEventFlagsGet( ( (BuzzerInstancePtr)buzzer )->statusHandle );
}

// ============================================================================
// Internal
// ============================================================================

/// @brief Load the next note from the current melody into the timer hardware.
///
/// For a rest, the GPIO is left high and the toggle counter is derived from
/// the duration divided by 10 ms. For a tone, the timer auto-reload register
/// is set to produce the correct half-period in microseconds.
///
/// @note Called only from within a critical section or from TimerHandler (ISR).
static void PrepareNote( BuzzerInstancePtr buzzer ) {
    const Melody*     melody = (const Melody*)buzzer->currentMelody;
    uint8_t           index  = buzzer->currentIndex;
    const BuzzerTone* note   = &melody->tones[ index ];

    if ( note->tone == NoteRest ) {
        osEventFlagsSet( buzzer->statusHandle, BIT( FlagBuzzerStatusRest ) );
        buzzer->remainingToggles = note->durationMs / 10;
        __HAL_TIM_SET_AUTORELOAD( &htim7, 10000 );
    } else {
        osEventFlagsClear( buzzer->statusHandle, BIT( FlagBuzzerStatusRest ) );
        buzzer->remainingToggles = ( ( uint32_t )note->tone * note->durationMs ) / 500;
        uint32_t period = 500000 / ( uint32_t )note->tone;
        __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    }

    __HAL_TIM_SET_COUNTER( &htim7, 0 );
}

/// @brief TIM7 period-elapsed callback — advances the melody sequencer for Buzzer1.
///
/// On each tick, toggles or silences the GPIO depending on the current note
/// type, decrements the remaining-toggle counter, and when it reaches zero
/// either loads the next note or stops the timer and clears the active flag.
///
/// @param[in] htim TIM handle (unused; htim7 is addressed directly).
/// @warning Called from ISR context. Must not call any FreeRTOS blocking API.
static void TimerHandler( TIM_HandleTypeDef *htim ) {
    UNUSED( htim );

    BuzzerInstancePtr inst = &instances[ Buzzer1 ];

    uint32_t flags = osEventFlagsGet( inst->statusHandle );
    if ( !( flags & BIT( FlagBuzzerStatusActive ) ) ) return;

    if ( !( flags & BIT( FlagBuzzerStatusRest ) ) ) {
        HAL_GPIO_TogglePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin );
    } else {
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    }

    if ( inst->remainingToggles > 0 ) {
        inst->remainingToggles--;
    } else {
        inst->currentIndex++;

        if ( inst->currentIndex < inst->currentMelody->length ) {
            PrepareNote( inst );
        } else {
            HAL_TIM_Base_Stop_IT( &htim7 );
            HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
            osEventFlagsClear( inst->statusHandle, BIT( FlagBuzzerStatusActive ) );
        }
    }
}

#endif // FEATURE_BUZZER
