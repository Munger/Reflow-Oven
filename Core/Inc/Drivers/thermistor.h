#ifndef THERMISTOR_H
#define THERMISTOR_H

#include "types.h"
#include "stm32g0xx_hal.h"

// Identifiers for the specific NTC thermistors in the system.
typedef enum {
    ThermistorCJT1 = 0,
    ThermistorCJT2,
    ThermistorOven
} ThermistorID;

// Status codes for Ntc validation and hardware health.
typedef enum {
    TMStatusOk = 0,
    TMStatusNoResponse,
    TMStatusOpenCircuit,
    TMStatusShortToGnd,
    TMStatusOutOfRange,
    TMStatusLowTemp,
    TMStatusHighTemp,
    TMStatusHardwareError
} ThermistorStatus;

// Opaque handle to an Ntc instance
typedef struct Thermistor* ThermistorRef;

// Initialise the NTC module's internal context and lookup tables.
void             TMInitModule( ADC_HandleTypeDef* hadc );

// Open a handle to a specific NTC thermistor.
ThermistorRef    TMOpen( ThermistorID thermistorID );

// Retrieve the processed temperature in milli-degrees Celsius.
Temperature      TMGetTemperature( ThermistorRef thermistor );

// Retrieve the health status of the thermistor.
ThermistorStatus TMGetStatus( ThermistorRef thermistor );

#endif // THERMISTOR_H