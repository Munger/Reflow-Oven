#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include <stdbool.h>

#include "types.h"
#include "SystemStatusFlags.h"

typedef enum {
    FlagREStatusReady = 0,
    FlagREStatusSpinning,
    FlagREStatusStall,
    FlagREStatusMagWeak,
    FlagREStatusMagStrong,
    FlagREStatusMagMissing,
    FlagREStatusHardwareFault,
    
    REFlagsCount
} REStatusBit;

extern osEventFlagsId_t OvenFanStatusFlagsHandle;

_Static_assert( REFlagsCount <= 24, "OvenFanStatusFlags out of bounds" );

typedef struct RotaryEncoder* RotaryEncoderRef;

void             REInitModule( void );
RotaryEncoderRef REOpen( void );

// Returns the rotational velocity. 
Rpm              REGetVelocity( RotaryEncoderRef encoder );

// The primary diagnostic interface for the supervisor.
uint32_t         REGetStatus( void );

// Called from task loop.
void REProcess( void );

#endif // ROTARYENCODER_H