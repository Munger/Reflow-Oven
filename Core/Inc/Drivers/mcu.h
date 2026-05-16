#ifndef MCU_H
#define MCU_H

#include <stdbool.h>

#include "types.h"
#include "SystemStatusFlags.h"

typedef enum {
    FlagMCUStatusReady = 0,
    FlagMCUStatusLowBattery,
    FlagMCUStatusCriticalBattery,
    FlagMCUStatusOverTemp,
    FlagMCUStatusVoltageUnstable,
    FlagMCUStatusRtcInvalid,
    FlagMCUStatusAdcError,
    
    MCUFlagsCount
} MCUStatusBit;

extern osEventFlagsId_t MCUStatusFlagsHandle;

_Static_assert( MCUFlagsCount <= 24, "MCUStatusFlags out of bounds" );

// Real-Time Clock structure for system logging and scheduling.
typedef struct {
    uint8_t  Hours;
    uint8_t  Minutes;
    uint8_t  Seconds;
    uint8_t  Day;
    uint8_t  Month;
    uint16_t Year;
} MCUTime, *MCUTimePtr;

// Initialise internal MCU peripherals (RTC, Internal ADC channels, etc).
void        MCUInitModule( void );

// Power & Environment Queries 
// These return normalised values as defined in types.h.
Temperature MCUGetInternalTemp( void );
Voltage     MCUGetVcc( void );
Voltage     MCUGetBatteryVoltage( void );
Permille    MCUGetBatteryLevel( void );
uint32_t    MCUGetStatus( void );

// Time Management Queries 
void        MCUGetTime( MCUTimePtr time );
void        MCUSetTime( const MCUTimePtr time );

// Called from task loop.
void MCUProcess( void );

#endif // MCU_H