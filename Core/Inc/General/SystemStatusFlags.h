#ifndef SYSTEM_STATUS_FLAGS_H
#define SYSTEM_STATUS_FLAGS_H

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "event_groups.h"

#define BIT( n ) ( 1UL << ( n ) )
#define OS_USER_FLAGS_MASK    0x00FFFFFFUL

typedef enum {
    FlagSystemInitialised = 0,    
    FlagInterruptsEnabled,        
    FlagSupervisorServiceRequest, 

    SystemFlagsCount 
} SystemFlagBit;

typedef enum {
    FlagMCUReady = 0,
    FlagUSBPDReady,
    FlagUSBPDContractReady,
    
    FlagThermocouple1Ready,
    FlagThermocouple2Ready,
    
    FlagThermistorCJT1Ready,
    FlagThermistorCJT2Ready,
    FlagThermistorOvenReady,
    FlagThermistorHeatsinkReady,

    FlagPowerManagerReady,
    FlagBuzzerReady,
 
    FlagOvenFanReady,
    FlagOvenFanSpinning,
    FlagBoardFanReady,

    DeviceFlagsCount 
} DeviceFlagsBit;

typedef enum {
    FlagReflowInProgress = 0,
    FlagReflowHeating,
    FlagReflowSoaking,
    FlagReflowCooling,
    FlagReflowDone,

    ReflowFlagsCount
} ReflowStatusBit;

typedef enum {
    FlagESTOP = 0,
    FlagMCUFault,
    FlagPowerManagerFault,
    FlagUSBPDFault,
    FlagThermocouple1Fault,
    FlagThermocouple2Fault,
    FlagThermistorCJT1Fault,
    FlagThermistorCJT2Fault,
    FlagThermistorOvenFault,
    FlagThermistorHeatsinkFault,
    FlagOvenFanFault,
    FlagBoardFanFault,
    FlagBuzzerFault,
    FlagReflowFault,
    FlagI2CFault,
    FlagSPIFault,
    FlagTriacFault,

    FaultFlagsCount
} FaultFlagsBit;

#define FAULT_ANY ( \
    BIT( FlagESTOP )                   | \
    BIT( FlagMCUFault )                | \
    BIT( FlagPowerManagerFault )       | \
    BIT( FlagUSBPDFault )              | \
    BIT( FlagThermocouple1Fault )      | \
    BIT( FlagThermocouple2Fault )      | \
    BIT( FlagThermistorCJT1Fault )     | \
    BIT( FlagThermistorCJT2Fault )     | \
    BIT( FlagThermistorOvenFault )     | \
    BIT( FlagThermistorHeatsinkFault ) | \
    BIT( FlagOvenFanFault )            | \
    BIT( FlagBoardFanFault )           | \
    BIT( FlagBuzzerFault )             | \
    BIT( FlagReflowFault )             | \
    BIT( FlagI2CFault )                | \
    BIT( FlagSPIFault )                | \
    BIT( FlagTriacFault )                \
)

extern osEventFlagsId_t SystemStatusFlagsHandle;
extern osEventFlagsId_t DeviceStatusFlagsHandle;
extern osEventFlagsId_t FaultFlagsHandle;
extern osEventFlagsId_t ReflowStatusFlagsHandle;

_Static_assert( SystemFlagsCount <= 24, "SystemStatusFlags out of bounds" );
_Static_assert( DeviceFlagsCount <= 24, "DeviceStatusFlags out of bounds" );
_Static_assert( ReflowFlagsCount <= 24, "ReflowStatusFlags out of bounds" );
_Static_assert( FaultFlagsCount  <= 24, "FaultStatusFlags out of bounds" );

#endif // SYSTEM_STATUS_FLAGS_H