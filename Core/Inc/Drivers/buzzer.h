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
/// These map 1:1 to the bits in the private buzzerStatus event flag group.
typedef enum {
    FlagBuzzerStatusReady = 0,      ///< Hardware / timer initialised
    FlagBuzzerStatusActive,          ///< Currently playing a note or melody
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

/// @brief Initialise the timer peripheral and GPIO for PWM output and prime the sequencer.
void BuzzerInitModule( void );

/// @brief Start the buzzer immediately at the specified note frequency.
/// @param[in] frequency Frequency in Hz; pass NoteRest to silence without stopping the timer.
void BuzzerStart( BuzzerFrequency frequency );

/// @brief Immediately stop the PWM signal and de-assert the output GPIO.
void BuzzerStop( void );

/// @brief Queue a pre-defined melodic pattern for asynchronous playback.
/// @param[in] pattern Pattern identifier from BuzzerPattern.
void BuzzerPlay( const BuzzerPattern pattern );

/// @brief Queue a custom melody sequence for asynchronous playback.
/// @param[in] melody Pointer to a Melody struct with at least one tone entry.
/// @note Playback is ISR-driven; this function returns immediately.
void BuzzerPlayMelody( const Melody* melody );

/// @brief Return the current bitmask from the internal buzzer status flags.
/// @return Bitmask of BuzzerStatusBit flags; safe to call from any task context.
uint32_t BuzzerGetStatus( void );

/// @brief Task-loop tick for the buzzer module (currently a no-op; sequencing is ISR-driven).
void BuzzerProcess( void );

#endif // BUZZER_H
