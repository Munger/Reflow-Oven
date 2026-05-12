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
    TCStatusCJTRangeLow,
    TCStatusCJTRangeHigh,
    TCStatusRangeLow,
    TCStatusRangeHigh,
    TCStatusCJTMismatch,
    TCStatusHardwareFault
} ThermocoupleStatus;

// Opaque handle to a thermocouple instance
typedef struct Thermocouple* ThermocoupleRef;

// Initialise the thermocouple module and internal memory pools.
void               TCInitModule( void );

// Open a handle to a specific thermocouple instance.
ThermocoupleRef    TCOpen( ThermocoupleID thermocoupleID );

// Triggers an asynchronous conversion.
void               TCRequestSample( ThermocoupleRef tc );

// Returns true if a new sample has been processed since the last request.
bool               TCIsReady( ThermocoupleRef tc );

// Retrieves the latest processed temperature. Blocks until DRDY if required.
Temperature        TCGetTemperature( ThermocoupleRef tc );

// Retrieves the latest internal cold junction temperature.
Temperature        TCGetCJT( ThermocoupleRef tc );

// Returns the hardware fault status of the MAX31856.
ThermocoupleStatus TCGetStatus( ThermocoupleRef tc );

// Hardware notification dispatcher.
// To be called by the EXTI interrupt handler with the triggered GPIO_Pin.
void               TCNotifyInterrupt( uint16_t pin );

#endif // THERMOCOUPLE_H