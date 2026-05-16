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

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "Platform.h"
#include "main.h"
#include "tim.h"
#include "Buzzer.h"

/// @brief Private event flag group for buzzer module status.
static osEventFlagsId_t buzzerStatus;

/// @brief Internal sequencer state — tracks progress through the active melody.
/// Declared volatile because it is written by the TIM7 ISR and read by task-level code.
static volatile struct {
    const Melody* currentMelody;    ///< Pointer to the melody currently being played
    uint8_t       currentIndex;     ///< Index of the note currently playing
    uint32_t      remainingToggles; ///< Timer ticks remaining for the current note
    bool          isRest;           ///< True when the current note is a silent rest
} sequencer;

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

static void TimerHandler( TIM_HandleTypeDef *htim );

/// @brief Initialise the timer peripheral and GPIO, create the private status flags group.
///
/// Resets the sequencer state, creates the buzzerStatus event group, configures
/// the BUZZER_EN_N GPIO to its idle-high (off) state, and registers the TIM7
/// period-elapsed callback. Signals DeviceStatusFlagsHandle on completion.
void BuzzerInitModule( void ) {
    memset( ( void* )&sequencer, 0, sizeof( sequencer ) );

    buzzerStatus = osEventFlagsNew( NULL );

    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    HAL_TIM_RegisterCallback( &htim7, HAL_TIM_PERIOD_ELAPSED_CB_ID, TimerHandler );

    // Internal ready flag only.
    osEventFlagsSet( buzzerStatus, BIT( FlagBuzzerStatusReady ) );
    // Signal system that hardware is initialized.
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBuzzerReady ) );
}

/// @brief Load the next note from the current melody into the timer hardware.
///
/// For a rest, the GPIO is left high and the toggle counter is derived from
/// the duration divided by 10 ms. For a tone, the timer auto-reload register
/// is set to produce the correct half-period in microseconds.
///
/// @note Called only from within a critical section or from TimerHandler (ISR).
static void BuzzerPrepareNote( void ) {
    const BuzzerTone* note = &sequencer.currentMelody->tones[ sequencer.currentIndex ];

    if ( note->tone == NoteRest ) {
        sequencer.isRest = true;
        sequencer.remainingToggles = note->durationMs / 10;
        __HAL_TIM_SET_AUTORELOAD( &htim7, 10000 );
    } else {
        sequencer.isRest = false;
        sequencer.remainingToggles = ( ( uint32_t )note->tone * note->durationMs ) / 500;
        uint32_t period = 500000 / ( uint32_t )note->tone;
        __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    }

    __HAL_TIM_SET_COUNTER( &htim7, 0 );
}

/// @brief Start the buzzer at a specific frequency, bypassing the sequencer.
///
/// Configures TIM7 for the requested frequency and starts it. The active flag
/// is set atomically inside a critical section.
///
/// @param[in] frequency Frequency in Hz; pass NoteRest to silence the output pin
///                      without starting the timer.
void BuzzerStart( BuzzerFrequency frequency ) {
    if ( frequency == NoteRest ) {
        taskENTER_CRITICAL();
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
        taskEXIT_CRITICAL();
        return;
    }

    uint32_t period = 500000 / ( uint32_t )frequency;
    taskENTER_CRITICAL();
    __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    osEventFlagsSet( buzzerStatus, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
    taskEXIT_CRITICAL();
}

/// @brief Immediately stop the PWM signal and de-assert the BUZZER_EN_N GPIO.
///
/// The active status flag is cleared atomically inside a critical section.
void BuzzerStop( void ) {
    taskENTER_CRITICAL();
    HAL_TIM_Base_Stop_IT( &htim7 );
    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    osEventFlagsClear( buzzerStatus, BIT( FlagBuzzerStatusActive ) );
    taskEXIT_CRITICAL();
}

/// @brief Queue a pre-defined melodic pattern for asynchronous ISR-driven playback.
/// @param[in] pattern Pattern identifier from BuzzerPattern.
void BuzzerPlay( const BuzzerPattern pattern ) {
    BuzzerPlayMelody( patterns[ ( uint8_t )pattern ] );
}

/// @brief Queue a custom melody for asynchronous ISR-driven playback.
///
/// Loads the melody into the sequencer and starts TIM7. The function returns
/// immediately; the ISR advances through notes autonomously.
///
/// @param[in] melody Pointer to a Melody struct. Must have at least one tone entry.
/// @warning The melody pointer must remain valid for the entire playback duration.
void BuzzerPlayMelody( const Melody* melody ) {
    if ( !melody || melody->length == 0 ) return;

    taskENTER_CRITICAL();
    sequencer.currentMelody = melody;
    sequencer.currentIndex  = 0;
    BuzzerPrepareNote();
    osEventFlagsSet( buzzerStatus, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
    taskEXIT_CRITICAL();
}

/// @brief Task-loop tick for the buzzer module.
/// @note Intentionally empty — all buzzer sequencing is handled by the TIM7 ISR.
void BuzzerProcess( void ) {
}

/// @brief TIM7 period-elapsed callback — advances the melody sequencer.
///
/// On each tick, toggles or silences the GPIO depending on the current note
/// type, decrements the remaining-toggle counter, and when it reaches zero
/// either loads the next note or stops the timer and clears the active flag.
///
/// @param[in] htim TIM handle (unused; htim7 is addressed directly).
/// @warning Called from ISR context. Must not call any FreeRTOS blocking API.
static void TimerHandler( TIM_HandleTypeDef *htim ) {
    UNUSED( htim );

    uint32_t flags = osEventFlagsGet( buzzerStatus );
    if ( !( flags & BIT( FlagBuzzerStatusActive ) ) ) return;

    if ( !sequencer.isRest ) {
        HAL_GPIO_TogglePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin );
    } else {
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    }

    if ( sequencer.remainingToggles > 0 ) {
        sequencer.remainingToggles--;
    } else {
        sequencer.currentIndex++;

        if ( sequencer.currentIndex < sequencer.currentMelody->length ) {
            BuzzerPrepareNote();
        } else {
            HAL_TIM_Base_Stop_IT( &htim7 );
            HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );

            osEventFlagsClear( buzzerStatus, BIT( FlagBuzzerStatusActive ) );
        }
    }
}

/// @brief Return the current status bitmask from the private buzzerStatus flags.
/// @return Bitmask of BuzzerStatusBit flags; safe to call from any task context.
uint32_t BuzzerGetStatus( void ) {
    return osEventFlagsGet( buzzerStatus );
}
