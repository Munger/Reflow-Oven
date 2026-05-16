/// @file DCFan.c
///
/// @brief EMC2101 DC fan controller — I2C async state-machine implementation.
///
/// Implements a four-state asynchronous I2C polling loop driven by DCFanProcess().
/// Temperature and tachometer data are read via I2CReadAsync() and stored in the
/// boardFan shadow struct. DCFanSetSpeed() only writes to requestedLevel; the
/// actual I2C duty cycle register write happens inside DCFanProcess(). All status
/// flags are set and cleared exclusively within DCFanProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "I2CManager.h"
#include "DCFan.h"
#include "main.h"

/// @brief EMC2101 I2C device address (7-bit, shifted left by 1 for HAL).
#define EMC2101_ADDR              ( 0x4C << 1 )

/// @name EMC2101 register addresses
/// @{
#define REG_INT_TEMP              0x00  ///< Internal die temperature (signed 8-bit, degrees C)
#define REG_EXT_TEMP_MSB          0x01  ///< External remote temperature MSB
#define REG_FAN_SETTING           0x19  ///< PWM duty cycle register (0=off, 255=full)
#define REG_FAN_CONFIG            0x20  ///< Fan configuration register
#define REG_TACH_LSB              0x46  ///< Tachometer period LSB (16-bit, combined with 0x47)
/// @}

/// @brief Constant used to convert tachometer period (counts) to RPM.
/// Formula: RPM = TACH_CONVERSION_CONST / tach_reading
#define TACH_CONVERSION_CONST     540000

/// @brief Number of evenly-spaced calibration steps used by DCFanCalibrate().
#define CAL_STEPS                 10

/// @brief RPM tolerance as a percentage of the lookup table value; readings below
/// (expected * PERFORMANCE_TOLERANCE_PCT / 100) trigger FlagDCFanStatusUnderSpeed.
#define PERFORMANCE_TOLERANCE_PCT 80

/// @brief Expected RPM at each calibration step (10% duty increments).
static const Rpm FanPerformanceTable[ CAL_STEPS ] = { 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000 };

#if CALIBRATION
/// @brief Stores measured RPM results for each calibration step (conditional compile).
static Rpm CalibrationResults[ CAL_STEPS ];
#endif

/// @brief Internal representation of a DC fan controller instance.
typedef struct DCFanController {
    DCFanID     id;            ///< Fan channel identifier
    Permille    requestedLevel; ///< Last speed requested via DCFanSetSpeed()
    Rpm         currentRpm;    ///< Most recently measured tachometer RPM
    Temperature internalTemp;  ///< EMC2101 die temperature in milli-degrees C
    Temperature externalTemp;  ///< EMC2101 remote thermistor temperature in milli-degrees C
} DCFanController, *DCFanControllerPtr;

/// @brief States of the asynchronous I2C polling state machine.
typedef enum {
    FanStateIdle = 0,        ///< Waiting to start a new polling cycle
    FanStateReadIntTemp,     ///< Async I2C read of internal temperature in progress
    FanStateReadExtTemp,     ///< Async I2C read of external temperature in progress
    FanStateReadTach,        ///< Async I2C read of tachometer register in progress
    FanStateProcessing       ///< All reads complete; processing results and updating flags
} FanIOState;

/// @brief Shared async I/O context written by FanI2CCallback() from ISR.
static volatile struct {
    FanIOState state;       ///< Current state machine position
    uint8_t    buffer[ 2 ]; ///< Raw bytes returned by the last I2C read
    bool       done;        ///< Set true by FanI2CCallback on success
    bool       error;       ///< Set true by FanI2CCallback on failure
} ioContext;

/// @brief Singleton fan controller instance for the board cooling fan.
static DCFanController boardFan;

/// @brief Flag set by DCFanSetSpeed() and cleared by DCFanProcess() after applying the new duty.
static volatile bool pendingSpeedSet = false;

/// @brief Private event flag group for board fan status.
/// @note Replaces the former public BoardFanStatusFlagsHandle extern.
static osEventFlagsId_t boardFanStatus;

static void FanI2CCallback( bool success );

/// @brief Initialise the fan controller, create status flags, and configure the EMC2101.
///
/// Checks for mains power presence before attempting I2C communication — the fan
/// is only powered when mains is detected. Sets FlagDCFanStatusReady and signals
/// DeviceStatusFlagsHandle on successful configuration.
void DCFanInitModule( void ) {
    boardFanStatus = osEventFlagsNew( NULL );
    boardFan.id = BoardCoolingFan;

    osEventFlagsClear( boardFanStatus, 0xFFFFFF );

    // Check for mains power (Active Low)
    if ( HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin ) == GPIO_PIN_SET ) {
        return;
    }

    uint8_t           config = 0x00;
    HAL_StatusTypeDef status = I2CWriteSync( EMC2101_ADDR, REG_FAN_CONFIG, 1, &config, 1, 100 );

    if ( status == HAL_OK ) {
        osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusReady ) );
        osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBoardFanReady ) );
    }
}

/// @brief Return a reference to the requested fan instance.
/// @param[in] fanID Fan channel identifier.
/// @return Pointer to the DCFanController, or NULL if the ID is invalid.
DCFanRef DCFanOpen( DCFanID fanID ) {
    if ( fanID == BoardCoolingFan ) {
        return &boardFan;
    }
    return NULL;
}

/// @brief Queue a fan speed update; the I2C write is applied by DCFanProcess() on the next tick.
///
/// The speed is clamped to [0, 1000]. If the clamped value equals the current
/// requestedLevel, the write is suppressed. The critical section ensures that
/// requestedLevel and pendingSpeedSet are updated atomically.
///
/// @param[in] fan   Handle returned by DCFanOpen().
/// @param[in] speed Target duty cycle in permille (0 = off, 1000 = full speed).
/// @note Actual hardware change happens in DCFanProcess(). Safe to call from any task.
void DCFanSetSpeed( DCFanRef fan, Permille speed ) {
    if ( fan == NULL ) return;

    uint32_t flags = osEventFlagsGet( boardFanStatus );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) ) return;

    Permille newLevel = ( speed > 1000 ) ? 1000 : speed;
    if ( newLevel == fan->requestedLevel ) return;

    taskENTER_CRITICAL();
    fan->requestedLevel = newLevel;
    pendingSpeedSet     = true;
    taskEXIT_CRITICAL();
}

/// @brief Drive the I2C state machine, apply pending speed commands, and update status flags.
///
/// On each call: applies any pending duty-cycle write, checks for async errors,
/// and advances the polling state machine through int-temp / ext-temp / tachometer
/// reads. On reaching FanStateProcessing, evaluates all thresholds, updates the
/// boardFanStatus flags, and propagates to FaultFlagsHandle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void DCFanProcess( void ) {
    uint32_t flags = osEventFlagsGet( boardFanStatus );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) ) return;

    if ( pendingSpeedSet ) {
        pendingSpeedSet = false;
        uint8_t duty = (uint8_t)( ( (uint32_t)boardFan.requestedLevel * 255 ) / 1000 );
        if ( I2CWriteSync( EMC2101_ADDR, REG_FAN_SETTING, 1, &duty, 1, 50 ) != HAL_OK ) {
            osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }
    }

    if ( ioContext.error ) {
        osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusHardwareFault ) );
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

        // Over-temperature check: both internal die and remote thermistor
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

                // Compare against performance table at the nearest duty step
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

        osEventFlagsSet( boardFanStatus, set );
        osEventFlagsClear( boardFanStatus, clear );

        // Propagate stall and over-temperature conditions to the global fault bus
        if ( set & ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusOverTemp ) ) ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }

        ioContext.state = FanStateIdle;
    }
}

/// @brief I2CManager callback invoked on completion or failure of an async read.
///
/// Sets done or error in ioContext so that DCFanProcess() can advance the state machine
/// on the next task tick.
///
/// @param[in] success true if the read completed successfully, false on error.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void FanI2CCallback( bool success ) {
    if ( success ) {
        ioContext.done = true;
    } else {
        ioContext.error = true;
    }
}

/// @brief Return the most recently measured fan speed.
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Cached RPM value; 0 if @p fan is NULL.
/// @note Safe to call from any task context without blocking.
Rpm DCFanGetSpeed( DCFanRef fan ) {
    return fan ? fan->currentRpm : 0;
}

/// @brief Return the EMC2101 internal die temperature.
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Cached temperature in milli-degrees Celsius; 0 if @p fan is NULL.
/// @note Safe to call from any task context without blocking.
Temperature DCFanGetInternalTemp( DCFanRef fan ) {
    return fan ? fan->internalTemp : 0;
}

/// @brief Return the EMC2101 remote thermistor temperature.
/// @param[in] fan Handle returned by DCFanOpen().
/// @return Cached temperature in milli-degrees Celsius; 0 if @p fan is NULL.
/// @note Safe to call from any task context without blocking.
Temperature DCFanGetExternalTemp( DCFanRef fan ) {
    return fan ? fan->externalTemp : 0;
}

/// @brief Return the full status bitmask from the private boardFanStatus flags.
/// @return Bitmask of DCFanStatusBit flags; safe to call from any task context.
uint32_t DCFanGetStatus( void ) {
    return osEventFlagsGet( boardFanStatus );
}
