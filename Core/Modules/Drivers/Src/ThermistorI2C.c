/// @file ThermistorI2C.c
///
/// @brief MCP3221 I2C ADC heatsink thermistor — async state-machine implementation.
///
/// Implements a three-state asynchronous I2C polling loop: idle → read ADC →
/// process. TMI2CProcess() drives the state machine for all open instances and
/// evaluates the ADC value against open/short and over-temperature thresholds,
/// updating each instance's private status event flag group. All hardware access
/// is confined to TMI2CProcess(). TMI2CGetTemperature() and TMI2CGetRaw() return
/// cached values. If an NTC lookup table was supplied at TMI2COpen(), it is used
/// for temperature conversion; otherwise a linear approximation is applied.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "ThermistorI2C.h"
#include "I2CManager.h"

/// @brief MCP3221 I2C device address (7-bit, shifted left by 1 for HAL).
static const uint8_t kMcp3221Addr = 0x4D << 1;

/// @brief NTC table entries cover 0..4096 in steps of 128 ADC counts (33 entries total).
static const uint16_t kNtcTableStep = 128U;

/// @brief States of the asynchronous I2C polling state machine.
typedef enum {
    TMStateIdle = 0,       ///< Waiting to start a new ADC read cycle
    TMStateReadAdc,         ///< Waiting for the 2-byte MCP3221 read to complete
    TMStateProcessing       ///< Read complete; evaluating thresholds and updating flags
} TMState;

/// @brief Internal per-instance state of the I2C thermistor driver.
typedef struct ThermistorI2C {
    ThermistorI2CID  id;             ///< Instance identifier
    I2CRef           i2c;            ///< I2C bus handle acquired at TMI2COpen()
    NTCEntryPtr      ntcTable;       ///< Optional 33-entry NTC lookup table; NULL → linear approx
    osEventFlagsId_t statusHandle;   ///< Private event flag group for this instance
    AdcRaw           latestRaw;      ///< Most recently received 12-bit ADC value
    TMState          state;          ///< Current state machine position (task context only)
    volatile uint8_t buffer[ 2 ];   ///< Raw bytes from the last I2C read (ISR-written)
} ThermistorI2C, *ThermistorI2CPtr;

/// @brief All thermistor instances — indexed by ThermistorI2CID.
static ThermistorI2C instances[ ThermistorI2CCount ];

/// @brief I2CManager completion callback — sets FlagTMI2CIODone or FlagTMI2CIOError.
///
/// @param[in] success true if the I2C read completed without error.
/// @warning Called from HAL ISR context. osEventFlagsSet() only — no blocking API.
static void TMI2CCallback( bool success ) {
    osEventFlagsSet( instances[ ThermistorI2C1 ].statusHandle,
                     BIT( success ? FlagTMI2CIODone : FlagTMI2CIOError ) );
}

/// @brief Allocate per-instance resources. Does not access I2C hardware.
void TMI2CInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    instances[ ThermistorI2C1 ].id           = ThermistorI2C1;
    instances[ ThermistorI2C1 ].statusHandle = osEventFlagsNew( NULL );
}

/// @brief Open a handle to a specific thermistor instance.
///
/// On first call for a given ID: stores @p i2c and @p ntcTable in the instance.
/// Subsequent calls with the same ID return the existing instance without re-configuring.
///
/// @param[in] id       Thermistor instance identifier.
/// @param[in] i2c      I2C bus handle returned by I2COpen().
/// @param[in] ntcTable 33-entry NTC lookup table (128 ADC counts/step), or NULL for linear approx.
/// @return Handle to the instance, or NULL if @p id is out of range.
ThermistorI2CRef TMI2COpen( ThermistorI2CID id, I2CRef i2c, NTCEntryPtr ntcTable ) {
    if ( id >= ThermistorI2CCount ) return NULL;
    ThermistorI2CPtr tm = &instances[ id ];
    if ( tm->i2c == NULL ) {
        tm->i2c      = i2c;
        tm->ntcTable = ntcTable;
        osEventFlagsSet( tm->statusHandle, BIT( FlagTMI2CStatusReady ) );
        osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagThermistorHeatsinkReady ) );
    }
    return tm;
}

/// @brief Drive the I2C state machine, evaluate fault thresholds, and update status flags.
///
/// State machine cycle:
///   1. Idle → starts an async 2-byte MCP3221 read.
///   2. ReadAdc → on done: decodes the 12-bit value (bits[3:0] of byte 0 + byte 1).
///   3. Processing → evaluates open/short circuit and over-temperature thresholds,
///      updates the instance status flags, propagates to FaultFlagsHandle, resets to Idle.
///
/// Error recovery: on I2C failure, sets FaultFlagsHandle and FlagTMI2CStatusHardwareFault,
/// then resets the state machine to Idle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void TMI2CProcess( void ) {
    for ( uint8_t i = 0; i < ThermistorI2CCount; i++ ) {
        ThermistorI2CPtr tm = &instances[ i ];
        if ( tm->i2c == NULL ) continue;

        uint32_t flags = osEventFlagsGet( tm->statusHandle );

        if ( flags & BIT( FlagTMI2CIOError ) ) {
            osEventFlagsSet( tm->statusHandle, BIT( FlagTMI2CStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
            osEventFlagsClear( tm->statusHandle, BIT( FlagTMI2CIOError ) | BIT( FlagTMI2CIODone ) );
            tm->state = TMStateIdle;
        }

        switch ( tm->state ) {
            case TMStateIdle:
                osEventFlagsClear( tm->statusHandle, BIT( FlagTMI2CIODone ) | BIT( FlagTMI2CIOError ) );
                tm->state = TMStateReadAdc;
                if ( I2CReadAsync( tm->i2c, kMcp3221Addr, 0, I2C_MEMADD_SIZE_8BIT, (uint8_t*)tm->buffer, 2, TMI2CCallback ) != HAL_OK ) {
                    tm->state = TMStateIdle;
                }
                break;

            case TMStateReadAdc:
                if ( flags & BIT( FlagTMI2CIODone ) ) {
                    // MCP3221 output format: D11..D8 in bits[3:0] of byte 0; D7..D0 in byte 1
                    tm->latestRaw = ( ( uint16_t )( tm->buffer[ 0 ] & 0x0F ) << 8 ) | tm->buffer[ 1 ];
                    osEventFlagsClear( tm->statusHandle, BIT( FlagTMI2CIODone ) );
                    tm->state = TMStateProcessing;
                }
                break;

            case TMStateProcessing:
                break;
        }

        if ( tm->state == TMStateProcessing ) {
            uint32_t set   = BIT( FlagTMI2CStatusReady );
            uint32_t clear = BIT( FlagTMI2CStatusHardwareFault );

            // ADC near maximum → thermistor open circuit (pull-up dominates)
            if ( tm->latestRaw >= 4090 ) {
                set |= BIT( FlagTMI2CStatusOpenCircuit );
            } else {
                clear |= BIT( FlagTMI2CStatusOpenCircuit );
            }

            // ADC near zero → thermistor shorted
            if ( tm->latestRaw <= 5 ) {
                set |= BIT( FlagTMI2CStatusShortCircuit );
            } else {
                clear |= BIT( FlagTMI2CStatusShortCircuit );
            }

            if ( TMI2CGetTemperature( tm ) > 95000 ) {
                set |= BIT( FlagTMI2CStatusOverTemp );
            } else {
                clear |= BIT( FlagTMI2CStatusOverTemp );
            }

            osEventFlagsSet( tm->statusHandle, set );
            osEventFlagsClear( tm->statusHandle, clear );

            // Update global fault bit: any flag set other than Ready trips the fault
            if ( ( set & ~BIT( FlagTMI2CStatusReady ) ) != 0 ) {
                osEventFlagsSet( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
            } else {
                osEventFlagsClear( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
            }

            tm->state = TMStateIdle;
        }
    }
}

/// @brief Compute heatsink temperature from the latest raw ADC value.
///
/// If an NTC table was supplied at TMI2COpen(), interpolates within the 33-entry
/// table (128 ADC counts per step). Otherwise uses a linear approximation:
/// 120°C at ADC=0, 0°C at ADC=4000 (slope ≈ −30 milli-degrees per count).
///
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Temperature in milli-degrees Celsius; 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor ) {
    if ( !thermistor ) return 0;

    if ( thermistor->ntcTable != NULL ) {
        uint16_t    raw  = thermistor->latestRaw;
        uint8_t     idx  = (uint8_t)( raw / kNtcTableStep );
        if ( idx > 31 ) idx = 31;
        uint8_t     frac = (uint8_t)( raw % kNtcTableStep );
        Temperature t0   = thermistor->ntcTable[ idx ];
        Temperature t1   = thermistor->ntcTable[ idx + 1 ];
        return t0 + (Temperature)( ( t1 - t0 ) * frac / kNtcTableStep );
    }

    return ( Temperature )( 120000 - ( (int32_t)thermistor->latestRaw * 30 ) );
}

/// @brief Return the most recently read raw 12-bit ADC value.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Raw ADC value (0–4095); 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor ) {
    return thermistor ? thermistor->latestRaw : 0;
}

/// @brief Return the full status bitmask for this thermistor instance.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Bitmask of TMI2CStatusBit flags; safe to call from any task context.
uint32_t TMI2CGetStatus( ThermistorI2CRef thermistor ) {
    if ( thermistor == NULL ) return BIT( FlagTMI2CStatusHardwareFault );
    return osEventFlagsGet( thermistor->statusHandle );
}
