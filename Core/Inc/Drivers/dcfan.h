#ifndef DCFAN_H
#define DCFAN_H

#include "types.h"

// Identifiers for the DC fans in the system.
typedef enum {
    BoardCoolingFan = 0
} DCFanID;

// Status codes for the DC fan and controller health.
typedef enum {
    DCFanStatusOk = 0,
    DCFanStatusStalled,      // Tachometer indicates no movement
    DCFanStatusNoTach,       // No signal detected on Tach pin
    DCFanStatusUnderSpeed,   // RPM is significantly lower than requested
    DCFanStatusOverTemp,     // Controller internal silicon limit exceeded
    DCFanStatusHardwareFault
} DCFanStatus;

// Opaque handle to a DC fan controller instance.
typedef struct DCFanController* DCFanRef;

// Initialise the fan controller module and I2C communications.
void               DCFanInitModule( void );

// Open a handle to a specific DC fan controller.
// @param fanID The identifier for the fan (e.g. BoardCoolingFan).
DCFanRef           DCFanOpen( DCFanID fanID );

// Set the fan speed duty cycle.
// @param level Power in permille (0 = Off, 1000 = Full Power).
void               DCFanSetSpeed( DCFanRef fan, Permille level );

// Returns the current rotational speed from the Tachometer.
Rpm                DCFanGetSpeed( DCFanRef fan );

// Returns the internal silicon temperature of the EMC2101.
Temperature        DCFanGetInternalTemp( DCFanRef fan );

// Returns the temperature of the external sensing diode.
Temperature        DCFanGetExternalTemp( DCFanRef fan );

// Returns the operational status of the fan.
DCFanStatus        DCFanGetStatus( DCFanRef fan );

#endif // DCFAN_H