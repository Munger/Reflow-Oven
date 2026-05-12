#ifndef THERMOCOUPLE_H
#define THERMOCOUPLE_H

#include "types.h"

// Identifiers for the specific thermocouple channels.
typedef enum {
    Thermocouple1 = 0,
    Thermocouple2
} ThermocoupleID;

// Fault and status codes specific to the MAX31856 hardware logic.
typedef enum {
    TCStatusOK = 0,
    TCStatusOpenCircuit,
    TCStatusShortToGND,
    TCStatusShortToVCC,
    TCStatusCJTRangeLow,  // CJT temperature below limits
    TCStatusCJTRangeHigh, // CJT temperature above limits
    TCStatusRangeLow,     // Thermocouple temperature too low
    TCStatusRangeHigh,    // Thermocouple temperature too high
    TCStatusCJTMismatch,  // Logic failure between internal CJT and NTC reference
    TCStatusHardwareFault
} ThermocoupleStatus;

// Opaque handle to a thermocouple instance
typedef struct Thermocouple* ThermocoupleRef;

// Initialise the thermocouple module and internal memory pools.
void               TCInitModule( void );

// Open a handle to a specific thermocouple instance.
// @param thermocoupleID The hardware-mapped identifier (Thermocouple1 or Thermocouple2).
ThermocoupleRef    TCOpen( ThermocoupleID thermocoupleID );

// Triggers an asynchronous conversion.
// Non-blocking; the driver manages the SPI transaction and internal state.
void               TCRequestSample( ThermocoupleRef tc );

// Returns true if a new sample has been processed since the last request.
bool               TCIsReady( ThermocoupleRef tc );

// Retrieves the latest processed temperature.
// Internally accounts for cold junction compensation and validation.
Temperature        TCGetTemperature( ThermocoupleRef tc );

// Retrieves the latest internal cold junction temperature.
Temperature        TCGetCJT( ThermocoupleRef tc );

// Returns the hardware fault status of the MAX31856.
ThermocoupleStatus TCGetStatus( ThermocoupleRef tc );

// Hardware notification shim.
// To be called by the EXTI interrupt handler for the DRDY pins (PA11/PA12).
void               TCNotifyDataReady( ThermocoupleRef tc );

#endif // THERMOCOUPLE_H