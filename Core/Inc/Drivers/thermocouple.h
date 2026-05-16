#ifndef THERMOCOUPLE_H
#define THERMOCOUPLE_H

#include "types.h"
#include "SystemStatusFlags.h"

// Identifiers for the specific thermocouple channels.
typedef enum {
    Thermocouple1 = 0,
    Thermocouple2
} ThermocoupleID;

// Fault and status codes specific to the MAX31856 hardware logic.
// These map 1:1 to the bits in ThermocoupleXStatusFlagsHandle.
typedef enum {
    FlagTCStatusOK = 0,
    FlagTCStatusDataReady,      // Local DRDY bit for this specific channel
    FlagTCStatusOpenCircuit,    // MAX31856 OC bit
    FlagTCStatusShortToGND,     // MAX31856 SCG bit
    FlagTCStatusShortToVCC,     // MAX31856 SCV bit
    FlagTCStatusCJTRangeLow,    // Cold Junction Under Limit
    FlagTCStatusCJTRangeHigh,   // Cold Junction Over Limit
    FlagTCStatusRangeLow,       // Thermocouple Under Limit
    FlagTCStatusRangeHigh,      // Thermocouple Over Limit
    FlagTCStatusCJTMismatch,    // CJT Sensor/Hardware mismatch
    FlagTCStatusHardwareFault,  // General SPI or internal IC failure

    TCFlagsCount
} ThermocoupleStatusBit;

// Individual handles for high-resolution telemetry (1 word per channel)
extern osEventFlagsId_t Thermocouple1StatusFlagsHandle;
extern osEventFlagsId_t Thermocouple2StatusFlagsHandle;

_Static_assert( TCFlagsCount <= 24, "ThermocoupleStatusFlags out of bounds" );

// Opaque handle to a thermocouple instance
typedef struct Thermocouple* ThermocoupleRef;

// Initialise the thermocouple module and register ISR callbacks.
void               TCInitModule( void );

// Open a handle to a specific thermocouple instance.
ThermocoupleRef    TCOpen( ThermocoupleID thermocoupleID );

// Triggers an asynchronous conversion.
void               TCRequestSample( ThermocoupleRef tc );

// Returns true if a new sample has been processed (Checks FlagTCStatusDataReady).
bool               TCIsReady( ThermocoupleRef tc );

// Retrieves the latest processed temperature. 
Temperature        TCGetTemperature( ThermocoupleRef tc );

// Retrieves the latest internal cold junction temperature.
Temperature        TCGetCJT( ThermocoupleRef tc );

// Returns the full bitmask from the respective EventGroup.
uint32_t           TCGetStatus( ThermocoupleRef tc );

// Background processing for SPI transactions and state updates.
void               TCProcess( void );

// Interrupt handlers for hardware pins.
void               TCHandleDRDYInterrupt( uint16_t GPIO_Pin );
void               TCHandleFaultInterrupt( uint16_t GPIO_Pin );

#endif // THERMOCOUPLE_H