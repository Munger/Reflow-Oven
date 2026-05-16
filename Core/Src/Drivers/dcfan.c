#include <string.h>

#include "I2CManager.h"
#include "dcfan.h"
#include "main.h"

#define EMC2101_ADDR              ( 0x4C << 1 )
#define REG_INT_TEMP              0x00
#define REG_EXT_TEMP_MSB          0x01
#define REG_FAN_SETTING           0x19
#define REG_FAN_CONFIG            0x20
#define REG_TACH_LSB              0x46

#define TACH_CONVERSION_CONST     540000
#define CAL_STEPS                 10
#define PERFORMANCE_TOLERANCE_PCT 80

static const Rpm FanPerformanceTable[ CAL_STEPS ] = { 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000 };

#if CALIBRATION
static Rpm CalibrationResults[ CAL_STEPS ];
#endif

typedef struct DCFanController {
    DCFanID     id;
    Permille    requestedLevel;
    Rpm         currentRpm;
    Temperature internalTemp;
    Temperature externalTemp;
} DCFanController, *DCFanControllerPtr;

typedef enum {
    FanStateIdle = 0,
    FanStateReadIntTemp,
    FanStateReadExtTemp,
    FanStateReadTach,
    FanStateProcessing
} FanIOState;

static volatile struct {
    FanIOState state;
    uint8_t    buffer[ 2 ];
    bool       done;
    bool       error;
} ioContext;

static DCFanController boardFan;

static void FanI2CCallback( bool success );

// Initialises the fan controller and configures the EMC2101 via the I2C Manager
void DCFanInitModule( void ) {
    boardFan.id = BoardCoolingFan;

    if ( BoardFanStatusFlagsHandle == NULL ) {
        return;
    }

    osEventFlagsClear( BoardFanStatusFlagsHandle, 0xFFFFFF );

    // Check for mains power (Active Low)
    if ( HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin ) == GPIO_PIN_SET ) {
        return;
    }

    uint8_t           config = 0x00;
    HAL_StatusTypeDef status = I2CWriteSync( EMC2101_ADDR, REG_FAN_CONFIG, 1, &config, 1, 100 );

    if ( status == HAL_OK ) {
        osEventFlagsSet( BoardFanStatusFlagsHandle, BIT( FlagDCFanStatusReady ) );
        osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBoardFanReady ) );
    }
}

// Returns a reference to the requested fan instance
DCFanRef DCFanOpen( DCFanID fanID ) {
    if ( fanID == BoardCoolingFan ) {
        return &boardFan;
    }
    return NULL;
}

// Updates the fan duty cycle using a synchronous I2C write
void DCFanSetSpeed( DCFanRef fan, Permille speed ) {
    if ( fan == NULL )
        return;

    uint32_t flags = osEventFlagsGet( BoardFanStatusFlagsHandle );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) )
        return;

    Permille newLevel = ( speed > 1000 ) ? 1000 : speed;
    if ( newLevel == fan->requestedLevel )
        return;

    fan->requestedLevel = newLevel;
    uint8_t duty = (uint8_t)( ( (uint32_t)fan->requestedLevel * 255 ) / 1000 );

    HAL_StatusTypeDef status = I2CWriteSync( EMC2101_ADDR, REG_FAN_SETTING, 1, &duty, 1, 50 );

    if ( status != HAL_OK ) {
        osEventFlagsSet( BoardFanStatusFlagsHandle, BIT( FlagDCFanStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
    }
}

// State machine to poll temperatures and tachometer data using asynchronous I2C reads
void DCFanProcess( void ) {
    uint32_t flags = osEventFlagsGet( BoardFanStatusFlagsHandle );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) )
        return;

    if ( ioContext.error ) {
        osEventFlagsSet( BoardFanStatusFlagsHandle, BIT( FlagDCFanStatusHardwareFault ) );
        ioContext.error = false;
        ioContext.done = false;
        ioContext.state = FanStateIdle;
    }

    uint8_t* pBuf = (uint8_t*)ioContext.buffer;

    switch ( ioContext.state ) {
        case FanStateIdle:
            ioContext.done = false;
            ioContext.error = false;
            ioContext.state = FanStateReadIntTemp;
            if ( I2CReadAsync( EMC2101_ADDR, REG_INT_TEMP, 1, pBuf, 1, FanI2CCallback ) != HAL_OK ) {
                ioContext.state = FanStateIdle;
            }
            break;

        case FanStateReadIntTemp:
            if ( ioContext.done ) {
                boardFan.internalTemp = (Temperature)( (int8_t)ioContext.buffer[ 0 ] ) * 1000;
                ioContext.done = false;
                ioContext.state = FanStateReadExtTemp;
                if ( I2CReadAsync( EMC2101_ADDR, REG_EXT_TEMP_MSB, 1, pBuf, 1, FanI2CCallback ) != HAL_OK ) {
                    ioContext.state = FanStateIdle;
                }
            }
            break;

        case FanStateReadExtTemp:
            if ( ioContext.done ) {
                boardFan.externalTemp = (Temperature)( (int8_t)ioContext.buffer[ 0 ] ) * 1000;
                ioContext.done = false;
                ioContext.state = FanStateReadTach;
                if ( I2CReadAsync( EMC2101_ADDR, REG_TACH_LSB, 1, pBuf, 2, FanI2CCallback ) != HAL_OK ) {
                    ioContext.state = FanStateIdle;
                }
            }
            break;

        case FanStateReadTach:
            if ( ioContext.done ) {
                uint16_t reading = (uint16_t)( ioContext.buffer[ 1 ] << 8 | ioContext.buffer[ 0 ] );
                boardFan.currentRpm =
                    ( reading == 0xFFFF || reading == 0 ) ? 0 : (Rpm)( TACH_CONVERSION_CONST / reading );
                ioContext.state = FanStateProcessing;
            }
            break;

        case FanStateProcessing:
            break;
    }

    if ( ioContext.state == FanStateProcessing ) {
        uint32_t set = 0, clear = 0;

        if ( boardFan.internalTemp > 80000 || boardFan.externalTemp > 85000 ) {
            set |= BIT( FlagDCFanStatusOverTemp );
        } else {
            clear |= BIT( FlagDCFanStatusOverTemp );
        }

        if ( boardFan.currentRpm > 100 ) {
            set |= BIT( FlagDCFanStatusSpinning );
        } else {
            clear |= BIT( FlagDCFanStatusSpinning );
        }

        if ( boardFan.requestedLevel > 0 ) {
            if ( boardFan.currentRpm < 50 ) {
                set |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusNoTach ) );
            } else {
                clear |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusNoTach ) );

                uint8_t idx = (uint8_t)( boardFan.requestedLevel / 100 );
                if ( idx > 0 && idx <= CAL_STEPS ) {
                    Rpm      expected = FanPerformanceTable[ idx - 1 ];
                    uint32_t floor = ( (uint32_t)expected * PERFORMANCE_TOLERANCE_PCT ) / 100;
                    if ( boardFan.currentRpm < floor ) {
                        set |= BIT( FlagDCFanStatusUnderSpeed );
                    } else {
                        clear |= BIT( FlagDCFanStatusUnderSpeed );
                    }
                }
            }
        } else {
            clear |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusUnderSpeed ) );
        }

        osEventFlagsSet( BoardFanStatusFlagsHandle, set );
        osEventFlagsClear( BoardFanStatusFlagsHandle, clear );

        if ( set & ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusOverTemp ) ) ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }

        ioContext.state = FanStateIdle;
    }
}

// Callback triggered by the I2C Manager upon completion of asynchronous reads
static void FanI2CCallback( bool success ) {
    if ( success ) {
        ioContext.done = true;
    } else {
        ioContext.error = true;
    }
}

Rpm DCFanGetSpeed( DCFanRef fan ) {
    return fan ? fan->currentRpm : 0;
}

Temperature DCFanGetInternalTemp( DCFanRef fan ) {
    return fan ? fan->internalTemp : 0;
}

Temperature DCFanGetExternalTemp( DCFanRef fan ) {
    return fan ? fan->externalTemp : 0;
}

uint32_t DCFanGetStatus( void ) {
    return osEventFlagsGet( BoardFanStatusFlagsHandle );
}