#ifndef THERMISTORI2C_H
#define THERMISTORI2C_H

#include "types.h"

// Status codes for the I2C-based thermistor validation.
typedef enum {
    TMI2CStatusOk = 0,
    TMI2CStatusNoResponse, // I2C communication failure
    TMI2CStatusOpenCircuit,
    TMI2CStatusShortToGnd,
    TMI2CStatusOutOfRange
} ThermistorI2CStatus;

// Opaque handle to an I2C thermistor instance.
typedef struct ThermistorI2C* ThermistorI2CRef;

// Initialise the I2C thermistor module (MCP3221).
void                          TMI2CInitModule( void );

// Open a handle to the heatsink thermistor.
ThermistorI2CRef              TMI2COpen( void );

// Retrieve the processed temperature in milli-degrees Celsius.
// Accounts for the MCP3221 resolution and the specific parallel resistor network.
Temperature                   TMI2CGetTemperature( ThermistorI2CRef thermistor );

// Retrieve the health and communication status.
ThermistorI2CStatus           TMI2CGetStatus( ThermistorI2CRef thermistor );

// Returns the raw 12-bit ADC value from the MCP3221 for debugging.
AdcRaw                        TMI2CGetRaw( ThermistorI2CRef thermistor );

#endif // THERMISTORI2C_H