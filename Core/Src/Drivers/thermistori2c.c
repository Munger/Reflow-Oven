/// @file ThermistorI2C.c
///
/// @brief MCP3221 I2C ADC heatsink thermistor — async state-machine implementation.
///
/// Implements a three-state asynchronous I2C polling loop: idle → read ADC →
/// process. TMI2CProcess() drives the state machine and evaluates the ADC value
/// against open/short and over-temperature thresholds, updating the private
/// heatsinkStatus event flag group. All hardware access is confined to
/// TMI2CProcess(). TMI2CGetTemperature() and TMI2CGetRaw() return cached values.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "ThermistorI2C.h"
#include "I2CManager.h"

/// @brief MCP3221 I2C device address (7-bit, shifted left by 1 for HAL).
#define MCP3221_ADDR ( 0x4D << 1 )

/// @brief Private event flag group for heatsink thermistor status.
/// @note Replaces the former public ThermistorHeatsinkStatusFlagsHandle extern.
static osEventFlagsId_t heatsinkStatus;

/// @brief States of the asynchronous I2C polling state machine.
typedef enum {
    TMStateIdle = 0,       ///< Waiting to start a new ADC read cycle
    TMStateReadAdc,         ///< Waiting for the 2-byte MCP3221 read to complete
    TMStateProcessing       ///< Read complete; evaluating thresholds and updating flags
} TMState;

/// @brief Internal state of the singleton heatsink thermistor instance.
struct ThermistorI2C {
    AdcRaw  latestRaw;    ///< Most recently received 12-bit ADC value
    TMState state;        ///< Current state machine position
    uint8_t buffer[ 2 ]; ///< Raw bytes from the last I2C read
    bool    done;         ///< Set true by TMI2CCallback on successful I2C completion
    bool    error;        ///< Set true by TMI2CCallback on I2C error
};

/// @brief Singleton instance — not accessible outside this translation unit.
static struct ThermistorI2C instance;

/// @brief Initialise the MCP3221 module and create the private status flag group.
void TMI2CInitModule( void ) {
    heatsinkStatus = osEventFlagsNew( NULL );

    memset( &instance, 0, sizeof( struct ThermistorI2C ) );
    osEventFlagsClear( heatsinkStatus, 0xFFFFFF );
}

/// @brief I2CManager completion callback — signals done or error to the state machine.
///
/// @param[in] success true if the I2C read completed without error.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TMI2CCallback( bool success ) {
    if ( success ) {
        instance.done = true;
    } else {
        instance.error = true;
    }
}

/// @brief Return a handle to the singleton heatsink thermistor instance.
/// @return Always returns a valid non-NULL reference.
ThermistorI2CRef TMI2COpen( void ) {
    return &instance;
}

/// @brief Drive the I2C state machine, evaluate fault thresholds, and update status flags.
///
/// State machine cycle:
///   1. Idle → starts an async 2-byte MCP3221 read.
///   2. ReadAdc → on done: decodes the 12-bit value (MSN of first byte + second byte).
///   3. Processing → evaluates open/short circuit and over-temperature thresholds,
///      updates heatsinkStatus, propagates to FaultFlagsHandle, resets to Idle.
///
/// Error recovery: on I2C failure, sets FaultFlagsHandle and FlagTMI2CStatusHardwareFault,
/// then resets the state machine to Idle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void TMI2CProcess( void ) {
    if ( instance.error ) {
        osEventFlagsSet( heatsinkStatus, BIT( FlagTMI2CStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );

        instance.error = false;
        instance.done  = false;
        instance.state = TMStateIdle;
    }

    switch ( instance.state ) {
        case TMStateIdle:
            instance.done  = false;
            instance.error = false;
            instance.state = TMStateReadAdc;
            if ( I2CReadAsync( MCP3221_ADDR, 0, 0, instance.buffer, 2, TMI2CCallback ) != HAL_OK ) {
                instance.state = TMStateIdle;
            }
            break;

        case TMStateReadAdc:
            if ( instance.done ) {
                // MCP3221 output format: D11..D8 in bits[3:0] of byte 0; D7..D0 in byte 1
                instance.latestRaw = ( ( uint16_t )( instance.buffer[ 0 ] & 0x0F ) << 8 ) | instance.buffer[ 1 ];
                instance.state = TMStateProcessing;
            }
            break;

        case TMStateProcessing:
            break;
    }

    if ( instance.state == TMStateProcessing ) {
        uint32_t set   = BIT( FlagTMI2CStatusReady );
        uint32_t clear = BIT( FlagTMI2CStatusHardwareFault );

        // ADC near maximum → thermistor open circuit (pull-up dominates)
        if ( instance.latestRaw >= 4090 ) {
            set |= BIT( FlagTMI2CStatusOpenCircuit );
        } else {
            clear |= BIT( FlagTMI2CStatusOpenCircuit );
        }

        // ADC near zero → thermistor shorted
        if ( instance.latestRaw <= 5 ) {
            set |= BIT( FlagTMI2CStatusShortCircuit );
        } else {
            clear |= BIT( FlagTMI2CStatusShortCircuit );
        }

        Temperature temp = TMI2CGetTemperature( &instance );
        if ( temp > 95000 ) {
            set |= BIT( FlagTMI2CStatusOverTemp );
        } else {
            clear |= BIT( FlagTMI2CStatusOverTemp );
        }

        osEventFlagsSet( heatsinkStatus, set );
        osEventFlagsClear( heatsinkStatus, clear );

        // Update global fault bit: any local flag set (except Ready) trips the fault
        if ( ( set & ~BIT( FlagTMI2CStatusReady ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
        }

        instance.state = TMStateIdle;
    }
}

/// @brief Compute heatsink temperature from the latest raw ADC value.
///
/// Uses a simple linear approximation: 120°C at ADC=0, 0°C at ADC=4000.
/// The slope is approximately −30 milli-degrees per ADC count.
///
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Temperature in milli-degrees Celsius; 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor ) {
    if ( !thermistor ) return 0;

    int32_t val = ( int32_t )thermistor->latestRaw;
    return ( Temperature )( 120000 - ( val * 30 ) );
}

/// @brief Return the most recently read raw 12-bit ADC value.
/// @param[in] thermistor Handle returned by TMI2COpen().
/// @return Raw ADC value (0–4095); 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor ) {
    return ( thermistor ) ? thermistor->latestRaw : 0;
}

/// @brief Return the full status bitmask from the private heatsinkStatus flags.
/// @return Bitmask of TMI2CStatusBit flags; safe to call from any task context.
uint32_t TMI2CGetStatus( void ) {
    return osEventFlagsGet( heatsinkStatus );
}
