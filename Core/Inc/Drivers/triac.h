#ifndef TRIAC_H
#define TRIAC_H

#include "types.h"
#include "SystemStatusFlags.h"

typedef enum {
    FlagTriacStatusReady = 0,       // Driver initialised and handles mapped
    FlagTriacStatusActive,          // Power is requested (Manual ON or Burst ON)
    FlagTriacStatusGateOpen,        // Physical GPIO is LOW (Gate conducting)
    FlagTriacStatusZCDLost,         // AC line sync lost (Watchdog timeout)
    FlagTriacStatusConfigError,     // Logic failure (Invalid params in TriacRun)
    FlagTriacStatusPhaseAngle,      // Mode: Sequenced phase-angle control
    FlagTriacStatusPulsePending,    // Sequencer has queued this channel for a pulse
    FlagTriacStatusPulseActive,     // This specific channel is currently in its 100us pulse window
    
    TriacFlagsCount
} TriacStatusBit;

_Static_assert( TriacFlagsCount <= 24, "TriacStatusFlags out of bounds" );

typedef enum {
    TriacHeaterTop = 0,
    TriacHeaterRear,
    TriacHeaterBottom,
    TriacOvenFan,
    TriacLight,
    
    TriacCount
} TriacID;

typedef struct {
    uint16_t phaseDelayUs;  // Delay from ZCD to Gate Fire (0-10000us)
    uint8_t  burstOn;       // Number of half-cycles 'On' in the window
    uint8_t  burstWindow;   // Total size of the burst window
} TriacDriveParams, *TriacDriveParamsPtr;

typedef struct TriacDevice* TriacRef;

extern osEventFlagsId_t TriacHTopStatusFlagsHandle;
extern osEventFlagsId_t TriacHRearStatusFlagsHandle;
extern osEventFlagsId_t TriacHBotStatusFlagsHandle;
extern osEventFlagsId_t TriacFanStatusFlagsHandle;
extern osEventFlagsId_t TriacLightStatusFlagsHandle;

void     TriacInitModule( void );
TriacRef TriacOpen( TriacID id );

// Control
void     TriacOn( TriacRef triac );
void     TriacOff( TriacRef triac );
void     TriacRun( TriacRef triac, TriacDriveParams params );

uint32_t TriacGetStatus( TriacRef triac );
void     TriacProcess( void );
void     ZCDHandler( uint16_t GPIO_Pin );

#endif // TRIAC_H