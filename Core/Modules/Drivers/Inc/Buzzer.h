/// @file Buzzer.h
///
/// @brief Piezo buzzer driver with melodic sequencer.
///
/// Controls a timer-driven PWM buzzer via a TIM7 interrupt-based sequencer.
/// Callers queue melodies or pre-defined patterns; BuzzerProcess() is a
/// no-op tick (sequencing is fully ISR-driven). BuzzerGetStatus() returns
/// the cached flag bitmask safe to call from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef BUZZER_H
#define BUZZER_H

#include "Features.h"

#if FEATURE_BUZZER

#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Full chromatic scale for the buzzer's optimal frequency range (7th–8th octave).
/// Frequencies are in Hz. The suffix 's' denotes Sharp (#).
typedef enum {
    NoteRest = 0,     ///< Silent rest (no gate pulses)
    NoteC7   = 2093,
    NoteCs7  = 2217,
    NoteD7   = 2349,
    NoteDs7  = 2489,
    NoteE7   = 2637,
    NoteF7   = 2794,
    NoteFs7  = 2960,
    NoteG7   = 3136,
    NoteGs7  = 3322,
    NoteA7   = 3520,
    NoteAs7  = 3729,
    NoteB7   = 3951,
    NoteC8   = 4186,
    NoteCs8  = 4435,
    NoteD8   = 4699,
    NoteDs8  = 4978,
    NoteE8   = 5274
} BuzzerFrequency;

/// @brief Identifiers for the pre-defined system notification melodies.
typedef enum {
    BuzzerPatternPowerOn = 0,   ///< Three-note ascending tone played at startup
    BuzzerPatternSuccess,        ///< Two-note ascending confirmation tone
    BuzzerPatternError,          ///< Double low-frequency error beep
    BuzzerPatternCritical,       ///< Rapid high-frequency double pulse
    BuzzerPatternLevelComplete   ///< Extended celebratory melody
} BuzzerPattern;

/// @brief Fault and status flag bit positions for the buzzer module.
/// These map 1:1 to the bits in the per-instance status event flag group.
typedef enum {
    FlagBuzzerStatusReady = 0,      ///< Hardware / timer initialised
    FlagBuzzerStatusActive,          ///< Currently playing a note or melody
    FlagBuzzerStatusRest,            ///< Current sequencer note is a silent rest
    FlagBuzzerStatusMuted,           ///< Software mute is active
    FlagBuzzerStatusHardwareFault,   ///< Placeholder for future diagnostic hardware

    BuzzerFlagsCount
} BuzzerStatusBit;

_Static_assert( BuzzerFlagsCount <= 24, "BuzzerStatusFlags out of bounds" );

/// @brief A single note in a melody: a frequency and a hold duration.
typedef struct BuzzerTone {
    BuzzerFrequency tone;       ///< Note frequency (use NoteRest for silence)
    uint16_t        durationMs; ///< Duration to hold the note in milliseconds
} BuzzerTone, *BuzzerTonePtr;

/// @brief A melodic sequence with a fixed-length note array.
/// @note The flexible array member requires the struct to be defined in static storage.
typedef struct Melody {
    uint8_t    length;  ///< Actual number of notes in the sequence
    BuzzerTone tones[]; ///< Flexible array member for the tone sequence
} Melody, *MelodyPtr;

/// @brief Logical identifiers for buzzer instances.
typedef enum {
    Buzzer1 = 0, ///< Primary piezo buzzer
    BuzzerCount
} BuzzerID;

/// @brief Opaque handle to a buzzer instance.
typedef struct BuzzerInstance* BuzzerRef;

/// @brief Allocate per-instance resources and register the timer callback.
void BuzzerInitModule( void );

/// @brief Open a buzzer instance and perform one-time hardware initialisation.
///
/// Idempotent — subsequent calls with the same @p id return the existing handle.
///
/// @param[in] id  Buzzer instance identifier.
/// @return Handle to the instance; NULL if @p id is out of range.
BuzzerRef BuzzerOpen( BuzzerID id );

/// @brief Start the buzzer at a specific frequency, bypassing the sequencer.
/// @param[in] buzzer     Handle returned by BuzzerOpen().
/// @param[in] frequency  Frequency in Hz; pass NoteRest to silence without stopping the timer.
void BuzzerStart( BuzzerRef buzzer, BuzzerFrequency frequency );

/// @brief Immediately stop the PWM signal and de-assert the output GPIO.
/// @param[in] buzzer  Handle returned by BuzzerOpen().
void BuzzerStop( BuzzerRef buzzer );

/// @brief Queue a pre-defined melodic pattern for asynchronous playback.
/// @param[in] buzzer   Handle returned by BuzzerOpen().
/// @param[in] pattern  Pattern identifier from BuzzerPattern.
void BuzzerPlay( BuzzerRef buzzer, BuzzerPattern pattern );

/// @brief Queue a custom melody sequence for asynchronous playback.
///
/// Playback is ISR-driven; this function returns immediately.
///
/// @param[in] buzzer  Handle returned by BuzzerOpen().
/// @param[in] melody  Pointer to a Melody struct with at least one tone entry.
/// @warning The melody pointer must remain valid for the entire playback duration.
void BuzzerPlayMelody( BuzzerRef buzzer, const Melody* melody );

/// @brief Return the current bitmask from the instance status flags.
/// @param[in] buzzer  Handle returned by BuzzerOpen().
/// @return Bitmask of BuzzerStatusBit flags; safe to call from any task context.
uint32_t BuzzerGetStatus( BuzzerRef buzzer );

/// @brief Task-loop tick for the buzzer module (currently a no-op; sequencing is ISR-driven).
void BuzzerProcess( void );

#endif // FEATURE_BUZZER

#endif // BUZZER_H
