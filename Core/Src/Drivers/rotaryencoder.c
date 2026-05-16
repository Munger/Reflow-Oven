/// @file RotaryEncoder.c
///
/// @brief AS5600 magnetic rotary encoder — I2C async state-machine implementation.
///
/// Implements a four-state polling loop: idle → read angle → read status → process.
/// The 12-bit angle difference between consecutive reads (with wrap-around correction)
/// is converted to RPM in REProcess(). All magnetic health bits (MD, MH, ML) are
/// decoded from the status register and mapped to ovenFanStatus flags. Hardware faults
/// and global FaultFlagsHandle updates occur exclusively in REProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "RotaryEncoder.h"
#include "I2CManager.h"

/// @brief AS5600 I2C device address (7-bit, shifted left by 1 for HAL).
#define ENCODER_ADDR ( 0x36 << 1 )

/// @name AS5600 register addresses
/// @{
#define REG_ANGLE  0x0E   ///< 12-bit raw angle (MSB at 0x0E, LSB at 0x0F)
#define REG_STATUS 0x0B   ///< Status register: bit5=MD (magnet detected), bit4=ML (too close), bit3=MH (too far)
/// @}

/// @brief Velocity above this threshold sets FlagREStatusSpinning.
#define RE_SPINNING_THRESHOLD_RPM 100

/// @brief Private event flag group for oven fan / encoder status.
/// @note Replaces the former public OvenFanStatusFlagsHandle extern.
static osEventFlagsId_t ovenFanStatus;

/// @brief Internal state machine states for the async I2C polling loop.
typedef enum {
    REStateIdle = 0,        ///< Waiting to start a new angle read cycle
    REStateReadAngle,        ///< Waiting for 2-byte angle register read to complete
    REStateReadStatus,       ///< Waiting for 1-byte status register read to complete
    REStateProcessing        ///< All reads complete; computing velocity and updating flags
} REState;

/// @brief Full internal state of the singleton encoder instance.
typedef struct RotaryEncoder {
    uint16_t last_raw;          ///< Previous 12-bit angle value for delta computation
    uint32_t last_tick;         ///< Kernel tick at the time of the last angle sample
    Rpm      current_velocity;  ///< Most recently computed velocity in RPM
    REState  state;             ///< Current state machine position
    uint8_t  buffer[ 2 ];       ///< Raw bytes returned by the last I2C read
    uint8_t  status_reg;        ///< Last value read from REG_STATUS
    bool     done;              ///< Set true by REI2CCallback on successful I2C completion
    bool     error;             ///< Set true by REI2CCallback on I2C error
} RotaryEncoder;

/// @brief Singleton encoder instance — not accessible outside this translation unit.
static RotaryEncoder instance;

/// @brief I2CManager completion callback — signals done or error to the state machine.
///
/// @param[in] success true if the I2C read completed without error.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void REI2CCallback( bool success ) {
    if ( success ) {
        instance.done = true;
    } else {
        instance.error = true;
    }
}

/// @brief Initialise the encoder module, reset internal state, and create the status flags group.
void REInitModule( void ) {
    ovenFanStatus = osEventFlagsNew( NULL );

    memset( &instance, 0, sizeof( RotaryEncoder ) );
    instance.last_tick = osKernelGetTickCount();

    osEventFlagsClear( ovenFanStatus, 0xFFFFFF );
}

/// @brief Return a handle to the singleton encoder instance.
/// @return Always returns a valid non-NULL reference to the internal instance.
RotaryEncoderRef REOpen( void ) {
    return &instance;
}

/// @brief Drive the I2C state machine, update velocity, and update status flags.
///
/// State machine cycle:
///   1. Idle → starts an async 2-byte angle read.
///   2. ReadAngle → on done: computes delta angle, converts to RPM, starts status read.
///   3. ReadStatus → on done: transitions to Processing.
///   4. Processing → evaluates magnetic health bits and spinning threshold,
///      updates ovenFanStatus and FaultFlagsHandle, resets to Idle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void REProcess( void ) {
    if ( instance.error ) {
        osEventFlagsSet( ovenFanStatus, BIT( FlagREStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );

        instance.error = false;
        instance.done  = false;
        instance.state = REStateIdle;
    }

    switch ( instance.state ) {
        case REStateIdle:
            instance.done  = false;
            instance.error = false;
            instance.state = REStateReadAngle;
            if ( I2CReadAsync( ENCODER_ADDR, REG_ANGLE, 1, instance.buffer, 2, REI2CCallback ) != HAL_OK ) {
                instance.state = REStateIdle;
            }
            break;

        case REStateReadAngle:
            if ( instance.done ) {
                uint16_t current_raw = ( ( uint16_t )instance.buffer[ 0 ] << 8 ) | instance.buffer[ 1 ];
                uint32_t now = osKernelGetTickCount();
                uint32_t dt_ms = now - instance.last_tick;

                if ( dt_ms >= 10 ) {
                    // Compute the shortest angular delta with wrap-around correction
                    int32_t delta = ( int32_t )current_raw - ( int32_t )instance.last_raw;
                    if ( delta > 2048 )  delta -= 4096;
                    if ( delta < -2048 ) delta += 4096;

                    // Convert delta (4096 counts/rev) at dt_ms interval to RPM
                    instance.current_velocity = ( Rpm )( ( delta * 60000 ) / ( ( int32_t )dt_ms * 4096 ) );
                    instance.last_raw  = current_raw;
                    instance.last_tick = now;
                }

                instance.done  = false;
                instance.state = REStateReadStatus;
                if ( I2CReadAsync( ENCODER_ADDR, REG_STATUS, 1, &instance.status_reg, 1, REI2CCallback ) != HAL_OK ) {
                    instance.state = REStateIdle;
                }
            }
            break;

        case REStateReadStatus:
            if ( instance.done ) {
                instance.state = REStateProcessing;
            }
            break;

        case REStateProcessing:
            break;
    }

    if ( instance.state == REStateProcessing ) {
        uint32_t set   = BIT( FlagREStatusReady );
        uint32_t clear = BIT( FlagREStatusHardwareFault );

        // MD bit (bit 5): magnet detected. If clear, magnet is missing entirely.
        if ( !( instance.status_reg & 0x20 ) ) {
            set |= BIT( FlagREStatusMagMissing );
            clear |= ( BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong ) );
        } else {
            clear |= BIT( FlagREStatusMagMissing );
            // MH bit (bit 3): magnet too far (AGC at max → weak)
            if ( instance.status_reg & 0x08 ) {
                set |= BIT( FlagREStatusMagWeak );
                clear |= BIT( FlagREStatusMagStrong );
            // ML bit (bit 4): magnet too close (AGC at min → strong)
            } else if ( instance.status_reg & 0x10 ) {
                set |= BIT( FlagREStatusMagStrong );
                clear |= BIT( FlagREStatusMagWeak );
            } else {
                clear |= ( BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong ) );
            }
        }

        if ( abs( instance.current_velocity ) > RE_SPINNING_THRESHOLD_RPM ) {
            set |= BIT( FlagREStatusSpinning );
        } else {
            clear |= BIT( FlagREStatusSpinning );
        }

        osEventFlagsSet( ovenFanStatus, set );
        osEventFlagsClear( ovenFanStatus, clear );

        // Raise global fault if anything other than Ready or Spinning is set
        if ( ( set & ~( BIT( FlagREStatusReady ) | BIT( FlagREStatusSpinning ) ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        }

        instance.state = REStateIdle;
    }
}

/// @brief Return the most recently computed velocity.
/// @param[in] Encoder Handle returned by REOpen().
/// @return Cached velocity in RPM; 0 if @p Encoder is NULL.
/// @note Safe to call from any task context without blocking.
Rpm REGetVelocity( RotaryEncoderRef Encoder ) {
    return Encoder ? Encoder->current_velocity : 0;
}

/// @brief Return the full status bitmask from the private ovenFanStatus flags.
/// @return Bitmask of REStatusBit flags; safe to call from any task context.
uint32_t REGetStatus( void ) {
    return osEventFlagsGet( ovenFanStatus );
}
