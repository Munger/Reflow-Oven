#ifndef THERMISTOR_H
#define THERMISTOR_H

#include "SystemStatusFlags.h"
#include "types.h"

typedef enum {
    FlagTMStatusReady = 0,         // Module initialised and ADC sampling
    FlagTMStatusLowTemp,           // Temperature below plausible range
    FlagTMStatusHighTemp,          // Temperature above safety limit
    FlagTMStatusOpenCircuit,       // ADC value suggests disconnected probe
    FlagTMStatusShortCircuit,      // ADC value suggests shorted probe
    FlagTMStatusHardwareFault,     // Internal ADC or DMA peripheral error
    
    TMFlagsCount
} TMStatusBit;

_Static_assert( TMFlagsCount <= 24, "Thermistor status flags out of bounds" );

typedef enum {
    ThermistorCJT1 = 0,
    ThermistorCJT2,
    ThermistorOven,
    
    ThermistorCount
} ThermistorID;

// Event flag handles for each analog channel
extern osEventFlagsId_t ThermistorCJT1StatusFlagsHandle;
extern osEventFlagsId_t ThermistorCJT2StatusFlagsHandle;
extern osEventFlagsId_t ThermistorOvenStatusFlagsHandle;

typedef struct Thermistor* ThermistorRef;

// Initialise the NTC module's internal context and lookup tables.
void TMInitModule( void );

// Open a handle to a specific NTC thermistor.
ThermistorRef TMOpen( ThermistorID thermistorID );

// Retrieve the processed temperature in milli-degrees Celsius.
Temperature TMGetTemperature( ThermistorRef thermistor );

// Returns the current bitmask of status and health flags for a specific instance.
uint32_t TMGetStatus( ThermistorRef thermistor );

// Processes ADC DMA buffers, performs linearisation, and updates flags.
void TMProcess( void );

#endif // THERMISTOR_H