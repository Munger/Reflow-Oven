#ifndef ESTOP_H
#define ESTOP_H

#include "types.h"

// Status of the physical emergency stop loop.
typedef enum {
    EStopStatusActive = 0, // Loop is closed, system is running
    EStopStatusTripped     // Loop is open, power is hardware-killed
} EStopStatus;

// Initialise the E-Stop input pin and rising-edge interrupt.
void        EStopInitModule( void );

// Returns the current state of the E-Stop hardware.
EStopStatus EStopGetStatus( void );

// Returns true if the hardware interlock is allowing power.
bool        EStopIsSafe( void );

#endif // ESTOP_H