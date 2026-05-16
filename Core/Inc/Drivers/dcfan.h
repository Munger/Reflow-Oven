#ifndef DCFAN_H
#define DCFAN_H

#include <stdbool.h>

#include "types.h"
#include "SystemStatusFlags.h"

typedef enum {
    BoardCoolingFan = 0
} DCFanID;

typedef enum {
    FlagDCFanStatusReady = 0,
    FlagDCFanStatusSpinning,       // Tachometer confirms rotation > threshold
    FlagDCFanStatusStall,          // Requested > 0 but RPM is ~0
    FlagDCFanStatusNoTach,         // Open circuit or failed sensor
    FlagDCFanStatusUnderSpeed,     // RPM significantly below target
    FlagDCFanStatusOverTemp,       // EMC2101 internal limit
    FlagDCFanStatusHardwareFault,  // I2C communication failure
    
    DCFanFlagsCount
} DCFanStatusBit;

extern osEventFlagsId_t BoardFanStatusFlagsHandle;

_Static_assert( DCFanFlagsCount <= 24, "BoardFanStatusFlags out of bounds" );

typedef struct DCFanController* DCFanRef;

void               DCFanInitModule( void );
DCFanRef           DCFanOpen( DCFanID fanID );
void               DCFanSetSpeed( DCFanRef fan, Permille speed );
Rpm                DCFanGetSpeed( DCFanRef fan );
Temperature        DCFanGetInternalTemp( DCFanRef fan );
Temperature        DCFanGetExternalTemp( DCFanRef fan );
uint32_t           DCFanGetStatus( void );
void               DCFanCalibrate( DCFanRef fan );
void               DCFanProcess( void );

#endif // DCFAN_H