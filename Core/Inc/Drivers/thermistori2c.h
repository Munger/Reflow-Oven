#ifndef THERMISTORI2C_H
#define THERMISTORI2C_H

#include "types.h"
#include "SystemStatusFlags.h"

typedef enum {
    FlagTMI2CStatusReady = 0,         // Module initialised and communicating
    FlagTMI2CStatusOverTemp,          // Heatsink temperature exceeds safety limit
    FlagTMI2CStatusOpenCircuit,       // Thermistor probe disconnected (Input pulled to GND)
    FlagTMI2CStatusShortCircuit,      // Thermistor shorted (Input pulled to VCC)
    FlagTMI2CStatusHardwareFault,     // I2C communication failure (No ACK/Timeout)
    
    TMI2CFlagsCount
} TMI2CStatusBit;

extern osEventFlagsId_t ThermistorHeatsinkStatusFlagsHandle;

_Static_assert( TMI2CFlagsCount <= 24, "ThermistorHeatsinkStatusFlags out of bounds" );

typedef struct ThermistorI2C* ThermistorI2CRef;

// Initialises the I2C thermistor module (MCP3221).
void TMI2CInitModule( void );

// Open a handle to the heatsink thermistor.
ThermistorI2CRef TMI2COpen( void );

// Retrieve the processed temperature in milli-degrees Celsius.
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor );

// Returns the raw 12-bit ADC value from the MCP3221.
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor );

// Returns the current bitmask of heatsink status and health flags.
uint32_t TMI2CGetStatus( void );

// Called from task loop to drive the I2C state machine and update flags.
void TMI2CProcess( void );

#endif // THERMISTORI2C_H