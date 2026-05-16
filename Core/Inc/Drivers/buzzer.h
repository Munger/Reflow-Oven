#ifndef BUZZER_H
#define BUZZER_H

#include "types.h"
#include "SystemStatusFlags.h"

// Full chromatic scale for the buzzer's optimal frequency range.
// Frequencies in Hz. 's' denotes Sharp (#).
typedef enum {
    NoteRest = 0,
    NoteC7 = 2093,
    NoteCs7 = 2217,
    NoteD7 = 2349,
    NoteDs7 = 2489,
    NoteE7 = 2637,
    NoteF7 = 2794,
    NoteFs7 = 2960,
    NoteG7 = 3136,
    NoteGs7 = 3322,
    NoteA7 = 3520,
    NoteAs7 = 3729,
    NoteB7 = 3951,
    NoteC8 = 4186,
    NoteCs8 = 4435,
    NoteD8 = 4699,
    NoteDs8 = 4978,
    NoteE8 = 5274
} BuzzerFrequency;

// Pre-defined sequences for system notifications.
typedef enum {
    BuzzerPatternPowerOn = 0,
    BuzzerPatternSuccess,
    BuzzerPatternError,
    BuzzerPatternCritical,
    BuzzerPatternLevelComplete
} BuzzerPattern;

// Fault and status codes specific to the Buzzer module logic.
// These map 1:1 to the bits in BuzzerStatusFlagsHandle.
typedef enum {
    FlagBuzzerStatusReady = 0,   // Hardware/Timer initialised
    FlagBuzzerStatusActive,      // Currently playing a note or melody
    FlagBuzzerStatusMuted,       // Software mute active
    FlagBuzzerStatusHardwareFault, // Placeholder for future diagnostic hardware

    BuzzerFlagsCount
} BuzzerStatusBit;

// Individual handle for buzzer telemetry.
extern osEventFlagsId_t BuzzerStatusFlagsHandle;

_Static_assert( BuzzerFlagsCount <= 24, "BuzzerStatusFlags out of bounds" );

typedef struct BuzzerTone {
    BuzzerFrequency tone;
    uint16_t        durationMs; // Duration to hold the note in milliseconds
} BuzzerTone, *BuzzerTonePtr;

typedef struct Melody {
    uint8_t    length;  // Actual number of notes in the sequence
    BuzzerTone tones[]; // Flexible array member for the tone sequence
} Melody, *MelodyPtr;

// Initialise the timer peripheral and GPIO for PWM output.
void BuzzerInitModule( void );

// Starts the buzzer at a specific note frequency.
void BuzzerStart( BuzzerFrequency frequency );

// Immediately stops the PWM signal.
void BuzzerStop( void );

// Play a pre-defined melodic pattern asynchronously.
void BuzzerPlay( const BuzzerPattern pattern );

// Play a custom melody sequence asynchronously.
void BuzzerPlayMelody( const Melody* melody );

// Returns the full bitmask from the BuzzerStatusFlagsHandle.
uint32_t BuzzerGetStatus( void );

// Called from task loop to manage durations and sequences.
void BuzzerProcess( void );

#endif // BUZZER_H