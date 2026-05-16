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
static const uint8_t kEmc2101Addr = 0x4C << 1;

// ============================================================================
// EMC2101 register addresses
// ============================================================================

static const uint8_t kRegIntTemp    = 0x00U; ///< Internal die temperature (signed 8-bit, degrees C)
static const uint8_t kRegExtTempMsb = 0x01U; ///< External remote temperature MSB
static const uint8_t kRegFanSetting = 0x19U; ///< PWM duty cycle register (0=off, 255=full)
static const uint8_t kRegFanConfig  = 0x20U; ///< Fan configuration register
static const uint8_t kRegTachLsb    = 0x46U; ///< Tachometer period LSB (16-bit, combined with 0x47)

/// @brief RPM = kTachConversionConst / tach_reading
static const uint32_t kTachConversionConst = 540000U;

/// @brief Number of evenly-spaced calibration steps used by DCFanCalibrate().
enum { kCalSteps = 10 };

/// @brief RPM tolerance as a percentage; readings below (expected * kPerformanceTolerancePct / 100)
/// trigger FlagDCFanStatusUnderSpeed.
static const uint8_t kPerformanceTolerancePct = 80U;

/// @brief Expected RPM at each calibration step (10% duty increments).
static const Rpm FanPerformanceTable[ kCalSteps ] = { 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000 };

#if CALIBRATION
/// @brief Stores measured RPM results for each calibration step (conditional compile).
static Rpm CalibrationResults[ kCalSteps ];
#endif

/// @brief Internal representation of a DC fan controller instance.
typedef struct DCFanController {
    DCFanID     id;             ///< Fan channel identifier
    I2CRef      i2c;            ///< I2C bus handle acquired at DCFanOpen()
    Permille    requestedLevel; ///< Last speed requested via DCFanSetSpeed()
    Rpm         currentRpm;     ///< Most recently measured tachometer RPM
    Temperature internalTemp;   ///< EMC2101 die temperature in milli-degrees C
    Temperature externalTemp;   ///< EMC2101 remote thermistor temperature in milli-degrees C
} DCFanController, *DCFanControllerPtr;

/// @brief States of the asynchronous I2C polling state machine.
typedef enum {
    FanStateIdle = 0,        ///< Waiting to start a new polling cycle
    FanStateReadIntTemp,     ///< Async I2C read of internal temperature in progress
    FanStateReadExtTemp,     ///< Async I2C read of external temperature in progress
    FanStateReadTach,        ///< Async I2C read of tachometer register in progress
    FanStateProcessing       ///< All reads complete; processing results and updating flags
} FanIOState;

/// @brief Shared async I/O context — state is task-only; buffer is ISR-written via DMA callback.
static volatile struct {
    FanIOState state;       ///< Current state machine position (task context only)
    uint8_t    buffer[ 2 ]; ///< Raw bytes returned by the last I2C read (written from ISR)
} ioContext;

/// @brief Singleton fan controller instance for the board cooling fan.
static DCFanController boardFan;

/// @brief Private event flag group for board fan status.
static osEventFlagsId_t boardFanStatus;

static void FanI2CCallback( bool success );

/// @brief Allocate the status event flag group. Does not access I2C hardware.
void DCFanInitModule( void ) {
    boardFanStatus = osEventFlagsNew( NULL );
    boardFan.id    = BoardCoolingFan;
    osEventFlagsClear( boardFanStatus, 0xFFFFFF );
}

/// @brief Open a handle to the specified fan channel and configure the EMC2101.
///
/// On first call for a given ID: stores @p i2c, checks mains power, writes the
/// fan configuration register, and signals DeviceStatusFlagsHandle on success.
/// Subsequent calls with the same ID return the existing instance without re-configuring.
///
/// @param[in] fanID Fan channel identifier.
/// @param[in] i2c   I2C bus handle returned by I2COpen().
/// @return Pointer to the DCFanController, or NULL if the ID is invalid.
DCFanRef DCFanOpen( DCFanID fanID, I2CRef i2c ) {
    if ( fanID != BoardCoolingFan ) return NULL;

    if ( boardFan.i2c == NULL ) {
        boardFan.i2c = i2c;

        // Fan is only powered when mains is detected (active-low signal)
        if ( HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin ) == GPIO_PIN_SET ) {
            return &boardFan;
        }

        uint8_t config = 0x00;
        if ( I2CWriteSync( i2c, kEmc2101Addr, kRegFanConfig, I2C_MEMADD_SIZE_8BIT, &config, 1, 100 ) == HAL_OK ) {
            osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusReady ) );
            osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBoardFanReady ) );
        }
    }

    return &boardFan;
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
    taskEXIT_CRITICAL();
    osEventFlagsSet( boardFanStatus, BIT( FlagDCFanSpeedPending ) );
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

    if ( flags & BIT( FlagDCFanSpeedPending ) ) {
        osEventFlagsClear( boardFanStatus, BIT( FlagDCFanSpeedPending ) );
        uint8_t duty = (uint8_t)( ( (uint32_t)boardFan.requestedLevel * 255 ) / 1000 );
        if ( I2CWriteSync( boardFan.i2c, kEmc2101Addr, kRegFanSetting, I2C_MEMADD_SIZE_8BIT, &duty, 1, 50 ) != HAL_OK ) {
            osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }
    }

    if ( flags & BIT( FlagDCFanIOError ) ) {
        osEventFlagsSet( boardFanStatus, BIT( FlagDCFanStatusHardwareFault ) );
        osEventFlagsClear( boardFanStatus, BIT( FlagDCFanIOError ) | BIT( FlagDCFanIODone ) );
        ioContext.state = FanStateIdle;
    }

    uint8_t* pBuf = (uint8_t*)ioContext.buffer;

    switch ( ioContext.state ) {
        case FanStateIdle:
            osEventFlagsClear( boardFanStatus, BIT( FlagDCFanIODone ) | BIT( FlagDCFanIOError ) );
            ioContext.state = FanStateReadIntTemp;
            if ( I2CReadAsync( boardFan.i2c, kEmc2101Addr, kRegIntTemp, I2C_MEMADD_SIZE_8BIT, pBuf, 1, FanI2CCallback ) != HAL_OK ) {
                ioContext.state = FanStateIdle;
            }
            break;

        case FanStateReadIntTemp:
            if ( flags & BIT( FlagDCFanIODone ) ) {
                boardFan.internalTemp = (Temperature)( (int8_t)ioContext.buffer[ 0 ] ) * 1000;
                osEventFlagsClear( boardFanStatus, BIT( FlagDCFanIODone ) );
                ioContext.state = FanStateReadExtTemp;
                if ( I2CReadAsync( boardFan.i2c, kEmc2101Addr, kRegExtTempMsb, I2C_MEMADD_SIZE_8BIT, pBuf, 1, FanI2CCallback ) != HAL_OK ) {
                    ioContext.state = FanStateIdle;
                }
            }
            break;

        case FanStateReadExtTemp:
            if ( flags & BIT( FlagDCFanIODone ) ) {
                boardFan.externalTemp = (Temperature)( (int8_t)ioContext.buffer[ 0 ] ) * 1000;
                osEventFlagsClear( boardFanStatus, BIT( FlagDCFanIODone ) );
                ioContext.state = FanStateReadTach;
                if ( I2CReadAsync( boardFan.i2c, kEmc2101Addr, kRegTachLsb, I2C_MEMADD_SIZE_8BIT, pBuf, 2, FanI2CCallback ) != HAL_OK ) {
                    ioContext.state = FanStateIdle;
                }
            }
            break;

        case FanStateReadTach:
            if ( flags & BIT( FlagDCFanIODone ) ) {
                uint16_t reading = (uint16_t)( ioContext.buffer[ 1 ] << 8 | ioContext.buffer[ 0 ] );
                boardFan.currentRpm =
                    ( reading == 0xFFFF || reading == 0 ) ? 0 : (Rpm)( kTachConversionConst / reading );
                osEventFlagsClear( boardFanStatus, BIT( FlagDCFanIODone ) );
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
                if ( idx > 0 && idx <= kCalSteps ) {
                    Rpm      expected = FanPerformanceTable[ idx - 1 ];
                    uint32_t floor = ( (uint32_t)expected * kPerformanceTolerancePct ) / 100;
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

/// @brief I2CManager callback — sets FlagDCFanIODone or FlagDCFanIOError in boardFanStatus.
///
/// @param[in] success true if the read completed successfully, false on error.
/// @warning Called from HAL ISR context. osEventFlagsSet() only — no blocking API.
static void FanI2CCallback( bool success ) {
    osEventFlagsSet( boardFanStatus, BIT( success ? FlagDCFanIODone : FlagDCFanIOError ) );
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
