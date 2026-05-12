#ifndef MCU_H
#define MCU_H

#include "types.h"

// System power and health status.
typedef enum {
    McuStatusOk = 0,
    McuStatusLowBattery,
    McuStatusCriticalBattery,
    McuStatusOverTemp,
    McuStatusVoltageUnstable
} McuStatus;

// Real-Time Clock structure for system logging and scheduling.
typedef struct {
    uint8_t  Hours;
    uint8_t  Minutes;
    uint8_t  Seconds;
    uint8_t  Day;
    uint8_t  Month;
    uint16_t Year;
} McuTime;

// Initialise internal MCU peripherals (RTC, Internal ADC channels, etc).
void        McuInit( void );

// Power & Environment Queries 
// These return normalised values as defined in types.h.
Temperature McuGetInternalTemp( void );
Voltage     McuGetVcc( void );
Voltage     McuGetBatteryVoltage( void );
Permille    McuGetBatteryLevel( void );
McuStatus   McuGetStatus( void );

// Time Management Queries 
void        McuGetTime( McuTime* Time );
void        McuSetTime( const McuTime* Time );

#endif // MCU_H