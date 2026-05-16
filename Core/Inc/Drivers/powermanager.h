#ifndef POWERMANAGER_H
#define POWERMANAGER_H

#include <stdbool.h>

#include "types.h"
#include "SystemStatusFlags.h"

// Granular status indices for the Power Manager.
// These represent the bit positions (0-23) in the PowerManagerStatusFlagsHandle.
typedef enum {
    FlagPMStatusReady = 0,
    
    // Hardware Inputs & Feedback
    FlagPMEStopTripped,      // Physical loop is open
    FlagPMMainsPower,        // Detected AC Mains input (vs USB)
    FlagPMSwitchedACLive,    // ZCD confirms power is actually at the output
    
    // Command States
    FlagPMHotSideEnabled,    // Relay command is active
    FlagPMAuxPowerEnabled,   // Aux rail is active
    
    // Logic/Fault States (Derived)
    FlagPMHotSideBlocked,    // E-Stop is preventing the HotSide from turning on
    FlagPMHotSideRogue,      // LIVE when it should be DISABLED
    FlagPMHotSideDead,       // DEAD when it should be ENABLED

    PMFlagsCount
} PMStatusBit;

// Individual handle for Power Manager high-resolution telemetry.
extern osEventFlagsId_t PowerManagerStatusFlagsHandle;

_Static_assert( PMFlagsCount <= 24, "PowerManagerStatusFlags out of bounds" );

// Initialise the Power Manager and associated interrupts.
void             PMInitModule( void );

// Commands
bool             PMEnableHotSide( void );
bool             PMDisableHotSide( void );
bool             PMEnableAuxPower( void );
bool             PMDisableAuxPower( void );

// Retrieves the full bitmask from PowerManagerStatusFlagsHandle.
uint32_t         PMGetStatus( void );

// Called from task loop for state reconciliation.
void             PMProcess( void );

// Interrupt handlers.
void             PMHandleZCDInterrupt( uint16_t GPIO_Pin );
void             PMHandleEStopInterrupt( uint16_t GPIO_Pin );

#endif // POWERMANAGER_H