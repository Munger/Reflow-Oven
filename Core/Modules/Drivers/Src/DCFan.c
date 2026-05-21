/// @file DCFan.c
///
/// @brief EMC2101 DC fan controller — I2C async state-machine implementation.
///
/// Implements a four-phase asynchronous I2C polling loop driven by DCFanProcess().
/// The current phase (ReadIntTemp, ReadExtTemp, ReadTach, Processing) is encoded
/// as mutually-exclusive flag bits in the per-instance statusHandle event group;
/// no separate state enum is needed. Temperature and tachometer data are read via
/// I2CReadAsync() and stored directly in the DCFanController instance struct.
/// DCFanSetSpeed() only writes to requestedPercent; the actual I2C duty-cycle
/// register write happens inside DCFanProcess(). All status flags are set and
/// cleared exclusively within DCFanProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_BOARD_FAN

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "I2CAddress.h"
#include "I2CManager.h"
#include "DCFan.h"
#include "main.h"
#include "cmsis_os.h"
#include "event_groups.h"
#include "TaskUtils.h"

#if FEATURE_FILE_SYSTEM
#include "FSUtils.h"
#endif // FEATURE_FILE_SYSTEM

static const uint16_t kEmc2101Addr = (uint16_t)I2CAddrEMC2101 << 1;

// ============================================================================
// EMC2101 register addresses
// ============================================================================

static const uint8_t kRegIntTemp    = 0x00U; ///< Internal die temperature (signed 8-bit, °C)
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

/// @brief Default expected RPM at each calibration step (10% duty increments).
/// Used when no calibration file has been persisted.
static const Rpm kDefaultPerformanceTable[ kCalSteps ] = { 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000 };

/// @brief Bitmask of all phase flags; used to determine whether the machine is idle.
static const uint32_t kPhaseMask =
    BIT( FlagDCFanPhaseReadIntTemp ) | BIT( FlagDCFanPhaseReadExtTemp ) |
    BIT( FlagDCFanPhaseReadTach )    | BIT( FlagDCFanPhaseProcessing );

/// @brief Internal representation of a DC fan controller instance.
typedef struct DCFanController {
    DCFanID          id;             ///< Fan channel identifier
    I2CRef           i2c;            ///< I2C bus handle acquired at DCFanOpen()
    osEventFlagsId_t statusHandle;   ///< Per-instance event flag group
    StaticEventGroup_t statusBuffer; ///< Storage backing statusHandle (no-heap allocation)
    Percent          requestedPercent; ///< Last speed requested via DCFanSetSpeed()
    Rpm              currentRpm;     ///< Most recently measured tachometer RPM
    Temperature      internalTemp;   ///< EMC2101 die temperature in milli-degrees C
    Temperature      externalTemp;   ///< EMC2101 remote thermistor temperature in milli-degrees C
    Rpm              performanceTable[ kCalSteps ]; ///< Calibrated RPM at each 10% duty step.
    volatile uint8_t buffer[ 2 ];                   ///< Raw bytes from the last I2C read (ISR-written)
} DCFanController, *DCFanControllerPtr;

/// @brief All fan controller instances — indexed by DCFanID.
static DCFanController instances[ DCFanCount ];

static void FanI2CCallback( bool success );

/// @brief Allocate per-instance resources. Does not access I2C hardware.
void DCFanInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    instances[ BoardCoolingFan ].id           = BoardCoolingFan;
    instances[ BoardCoolingFan ].statusHandle = osEventFlagsNew( &(osEventFlagsAttr_t){ .cb_mem = &instances[ BoardCoolingFan ].statusBuffer, .cb_size = sizeof( StaticEventGroup_t ) } );
    memcpy( instances[ BoardCoolingFan ].performanceTable, kDefaultPerformanceTable, sizeof( Rpm ) * kCalSteps );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBoardFanReady ) );
}

/// @brief Open a handle to the specified fan channel and configure the EMC2101.
///
/// On first call for a given ID: stores @p i2c, checks mains power, writes the
/// fan configuration register, and signals DeviceStatusFlagsHandle on success.
/// Subsequent calls with the same ID return the existing instance without re-configuring.
DCFanRef DCFanOpen( DCFanID fanID, I2CRef i2c ) {
    if ( fanID >= DCFanCount ) return NULL;
    DCFanControllerPtr fan = &instances[ fanID ];

    if ( fan->i2c == NULL ) {
        fan->i2c = i2c;

        // Fan is only powered when mains is detected (active-low signal)
        if ( HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin ) == GPIO_PIN_SET ) {
            return fan;
        }

        uint8_t config = 0x00;
        if ( I2CWriteSync( i2c, kEmc2101Addr, kRegFanConfig, I2C_MEMADD_SIZE_8BIT, &config, 1, 100 ) == HAL_OK ) {
            osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanStatusReady ) );

#if FEATURE_FILE_SYSTEM
            char   path[] = "/system/boardfan.profile";
            size_t bytesRead = 0;
            FSResult res = FSReadFile( path, 0, fan->performanceTable,
                                       sizeof( Rpm ) * kCalSteps, &bytesRead );
            if ( res != FSResultOk || bytesRead != sizeof( Rpm ) * kCalSteps ) {
                memcpy( fan->performanceTable, kDefaultPerformanceTable, sizeof( Rpm ) * kCalSteps );
            }
#endif // FEATURE_FILE_SYSTEM
        }
    }

    return fan;
}

/// @brief Queue a fan speed update; the I2C write is applied by DCFanProcess() on the next tick.
///
/// The speed is clamped to [0, 1000]. If the clamped value equals the current
/// requestedPercent, the write is suppressed. The critical section ensures that
/// requestedPercent and FlagDCFanSpeedPending are updated atomically.
/// @note Actual hardware change happens in DCFanProcess(). Safe to call from any task.
void DCFanSetSpeed( DCFanRef fan, Percent percent ) {
    if ( fan == NULL ) return;

    uint32_t flags = osEventFlagsGet( fan->statusHandle );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) ) return;

    Percent newLevel = ( percent > 100 ) ? 100 : percent;
    if ( newLevel == fan->requestedPercent ) return;

    taskENTER_CRITICAL();
    fan->requestedPercent = newLevel;
    taskEXIT_CRITICAL();
    osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanSpeedPending ) );
}

/// @brief Drive the I2C state machine, apply pending speed commands, and update status flags.
///
/// On each call: applies any pending duty-cycle write, checks for async errors,
/// and advances the polling loop through internal-temp / external-temp / tachometer
/// reads. On reaching the Processing phase, evaluates all thresholds, updates the
/// instance status flags, and propagates to FaultFlagsHandle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void DCFanProcess( void ) {
    DCFanControllerPtr fan = &instances[ BoardCoolingFan ];
    if ( fan->i2c == NULL ) return;

    uint32_t flags = osEventFlagsGet( fan->statusHandle );
    if ( !( flags & BIT( FlagDCFanStatusReady ) ) ) return;

    if ( flags & BIT( FlagDCFanSpeedPending ) ) {
        osEventFlagsClear( fan->statusHandle, BIT( FlagDCFanSpeedPending ) );
        uint8_t duty = (uint8_t)( ( (uint32_t)fan->requestedPercent * 255 ) / 100 );
        if ( I2CWriteSync( fan->i2c, kEmc2101Addr, kRegFanSetting, I2C_MEMADD_SIZE_8BIT, &duty, 1, 50 ) != HAL_OK ) {
            osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }
    }

    if ( flags & BIT( FlagDCFanIOError ) ) {
        osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        osEventFlagsClear( fan->statusHandle,
            BIT( FlagDCFanIOError ) | BIT( FlagDCFanIODone ) | kPhaseMask );
        return;
    }

    uint8_t* pBuf = (uint8_t*)fan->buffer;

    if ( !( flags & kPhaseMask ) ) {
        osEventFlagsClear( fan->statusHandle, BIT( FlagDCFanIODone ) | BIT( FlagDCFanIOError ) );
        if ( I2CReadAsync( fan->i2c, kEmc2101Addr, kRegIntTemp, I2C_MEMADD_SIZE_8BIT, pBuf, 1, FanI2CCallback ) == HAL_OK ) {
            osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanPhaseReadIntTemp ) );
        }
    } else if ( flags & BIT( FlagDCFanPhaseReadIntTemp ) ) {
        if ( flags & BIT( FlagDCFanIODone ) ) {
            fan->internalTemp = (Temperature)( (int8_t)fan->buffer[ 0 ] ) * 1000;
            osEventFlagsClear( fan->statusHandle, BIT( FlagDCFanIODone ) | BIT( FlagDCFanPhaseReadIntTemp ) );
            if ( I2CReadAsync( fan->i2c, kEmc2101Addr, kRegExtTempMsb, I2C_MEMADD_SIZE_8BIT, pBuf, 1, FanI2CCallback ) == HAL_OK ) {
                osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanPhaseReadExtTemp ) );
            }
        }
    } else if ( flags & BIT( FlagDCFanPhaseReadExtTemp ) ) {
        if ( flags & BIT( FlagDCFanIODone ) ) {
            fan->externalTemp = (Temperature)( (int8_t)fan->buffer[ 0 ] ) * 1000;
            osEventFlagsClear( fan->statusHandle, BIT( FlagDCFanIODone ) | BIT( FlagDCFanPhaseReadExtTemp ) );
            if ( I2CReadAsync( fan->i2c, kEmc2101Addr, kRegTachLsb, I2C_MEMADD_SIZE_8BIT, pBuf, 2, FanI2CCallback ) == HAL_OK ) {
                osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanPhaseReadTach ) );
            }
        }
    } else if ( flags & BIT( FlagDCFanPhaseReadTach ) ) {
        if ( flags & BIT( FlagDCFanIODone ) ) {
            uint16_t reading = (uint16_t)( fan->buffer[ 1 ] << 8 | fan->buffer[ 0 ] );
            fan->currentRpm = ( reading == 0xFFFF || reading == 0 ) ? 0 : (Rpm)( kTachConversionConst / reading );
            osEventFlagsClear( fan->statusHandle, BIT( FlagDCFanIODone ) | BIT( FlagDCFanPhaseReadTach ) );
            osEventFlagsSet( fan->statusHandle, BIT( FlagDCFanPhaseProcessing ) );
            flags |= BIT( FlagDCFanPhaseProcessing );
        }
    }

    if ( flags & BIT( FlagDCFanPhaseProcessing ) ) {
        uint32_t set = 0, clear = 0;

        if ( fan->internalTemp > 80000 || fan->externalTemp > 85000 ) {
            set |= BIT( FlagDCFanStatusOverTemp );
        } else {
            clear |= BIT( FlagDCFanStatusOverTemp );
        }

        if ( fan->currentRpm > 100 ) {
            set |= BIT( FlagDCFanStatusSpinning );
        } else {
            clear |= BIT( FlagDCFanStatusSpinning );
        }

        if ( fan->requestedPercent > 0 ) {
            if ( fan->currentRpm < 50 ) {
                set |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusNoTach ) );
            } else {
                clear |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusNoTach ) );

                // Compare against performance table at the nearest duty step
                uint8_t idx = (uint8_t)( fan->requestedPercent / 10 );
                if ( idx > 0 && idx <= kCalSteps ) {
                    Rpm      expected = fan->performanceTable[ idx - 1 ];
                    uint32_t floor    = ( (uint32_t)expected * kPerformanceTolerancePct ) / 100;
                    if ( fan->currentRpm < floor ) {
                        set |= BIT( FlagDCFanStatusUnderSpeed );
                    } else {
                        clear |= BIT( FlagDCFanStatusUnderSpeed );
                    }
                }
            }
        } else {
            clear |= ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusUnderSpeed ) );
        }

        osEventFlagsSet( fan->statusHandle, set );
        osEventFlagsClear( fan->statusHandle, clear | BIT( FlagDCFanPhaseProcessing ) );

        // Propagate stall and over-temperature conditions to the global fault bus
        if ( set & ( BIT( FlagDCFanStatusStall ) | BIT( FlagDCFanStatusOverTemp ) ) ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagBoardFanFault ) );
        }
    }
}

/// @brief I2CManager completion callback — sets FlagDCFanIODone or FlagDCFanIOError.
///
/// @param[in] success true if the read completed successfully, false on error.
/// @warning Called from HAL ISR context. osEventFlagsSet() only — no blocking API.
static void FanI2CCallback( bool success ) {
    osEventFlagsSet( instances[ BoardCoolingFan ].statusHandle,
                     BIT( success ? FlagDCFanIODone : FlagDCFanIOError ) );
}

/// @brief Return the most recently measured fan speed.
/// @note Safe to call from any task context without blocking.
Rpm DCFanGetSpeed( DCFanRef fan ) {
    return fan ? fan->currentRpm : 0;
}

/// @brief Return the EMC2101 internal die temperature.
/// @note Safe to call from any task context without blocking.
Temperature DCFanGetInternalTemp( DCFanRef fan ) {
    return fan ? fan->internalTemp : 0;
}

/// @brief Return the EMC2101 external (remote) thermistor temperature.
/// @note Safe to call from any task context without blocking.
Temperature DCFanGetExternalTemp( DCFanRef fan ) {
    return fan ? fan->externalTemp : 0;
}

/// @brief Return the full status bitmask for this fan instance.
uint32_t DCFanGetStatus( DCFanRef fan ) {
    if ( fan == NULL ) return BIT( FlagDCFanStatusHardwareFault );
    return osEventFlagsGet( fan->statusHandle );
}

/// @brief Sweep all duty steps, record RPM at each, and update the performance table.
///
/// Drives the EMC2101 via synchronous I2C (bypassing the async state machine).
/// Between steps the task suspends via WaitForFlags — zero CPU waste — and
/// aborts early if FlagSystemAborted fires. After calibration the fan is left
/// off and the new performance table is used for under-speed detection.
void DCFanCalibrate( DCFanRef fan ) {
    if ( !fan ) return;
    DCFanControllerPtr inst = (DCFanControllerPtr)fan;
    if ( !inst->i2c ) return;
    Rpm              calResults[ kCalSteps ];

    for ( uint8_t step = 1; step <= kCalSteps; step++ ) {
        uint8_t duty = (uint8_t)( ( step * 255U ) / kCalSteps );
        if ( I2CWriteSync( inst->i2c, kEmc2101Addr, kRegFanSetting,
                           I2C_MEMADD_SIZE_8BIT, &duty, 1, 50 ) != HAL_OK ) {
            break;
        }

        // Suspend until RPM settles or system aborts
        if ( WaitForFlags( FaultFlagsHandle, BIT( FlagSystemAborted ), 500 ) ) {
            duty = 0;
            I2CWriteSync( inst->i2c, kEmc2101Addr, kRegFanSetting,
                          I2C_MEMADD_SIZE_8BIT, &duty, 1, 50 );
            return;
        }

        uint8_t tach[ 2 ] = { 0, 0 };
        if ( I2CReadSync( inst->i2c, kEmc2101Addr, kRegTachLsb, I2C_MEMADD_SIZE_8BIT,
                          tach, 2, 50 ) == HAL_OK ) {
            uint16_t reading = (uint16_t)( tach[ 1 ] << 8 | tach[ 0 ] );
            calResults[ step - 1 ] = ( reading == 0xFFFF || reading == 0 )
                ? 0
                : (Rpm)( kTachConversionConst / reading );
        }
    }

    uint8_t off = 0;
    I2CWriteSync( inst->i2c, kEmc2101Addr, kRegFanSetting,
                  I2C_MEMADD_SIZE_8BIT, &off, 1, 50 );

    for ( uint8_t i = 0; i < kCalSteps; i++ ) {
        inst->performanceTable[ i ] = calResults[ i ];
    }

#if FEATURE_FILE_SYSTEM
    char path[] = "/system/boardfan.profile";
    FSEnsureDir( "/system", 0 );
    FSWriteFile( path, 0, inst->performanceTable, sizeof( Rpm ) * kCalSteps );
#endif // FEATURE_FILE_SYSTEM
}

#endif // FEATURE_BOARD_FAN
