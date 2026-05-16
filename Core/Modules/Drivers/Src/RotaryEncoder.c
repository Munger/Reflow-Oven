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
static const uint8_t kEncoderAddr = 0x36 << 1;

// ============================================================================
// AS5600 register addresses
// ============================================================================

static const uint8_t kRegAngle  = 0x0EU; ///< 12-bit raw angle (MSB at 0x0E, LSB at 0x0F)
static const uint8_t kRegStatus = 0x0BU; ///< Status register: bit5=MD, bit4=ML (too close), bit3=MH (too far)

/// @brief Velocity above this threshold sets FlagREStatusSpinning.
static const Rpm kSpinningThresholdRpm = 100;

/// @brief Private event flag group for oven fan / encoder status.
static osEventFlagsId_t ovenFanStatus;

/// @brief Internal state machine states for the async I2C polling loop.
typedef enum {
    REStateIdle = 0,        ///< Waiting to start a new angle read cycle
    REStateReadAngle,        ///< Waiting for 2-byte angle register read to complete
    REStateReadStatus,       ///< Waiting for 1-byte status register read to complete
    REStateProcessing        ///< All reads complete; computing velocity and updating flags
} REState;

/// @brief Full internal state of one encoder instance.
typedef struct RotaryEncoder {
    RotaryEncoderID  id;             ///< Encoder identifier
    I2CRef           i2c;            ///< I2C bus handle acquired at REOpen()
    uint16_t         last_raw;       ///< Previous 12-bit angle value for delta computation
    uint32_t         last_tick;      ///< Kernel tick at the time of the last angle sample
    Rpm              current_velocity; ///< Most recently computed velocity in RPM
    REState          state;           ///< Current state machine position
    volatile uint8_t buffer[ 2 ];    ///< Raw bytes returned by the last I2C read (ISR-written)
    uint8_t          status_reg;     ///< Last value read from REG_STATUS
} RotaryEncoder, *RotaryEncoderPtr;

/// @brief All encoder instances — indexed by RotaryEncoderID.
static RotaryEncoder instances[ RotaryEncoderCount ];

/// @brief I2CManager completion callback — sets FlagREIODone or FlagREIOError in ovenFanStatus.
///
/// @param[in] success true if the I2C read completed without error.
/// @warning Called from HAL ISR context. osEventFlagsSet() only — no blocking API.
static void REI2CCallback( bool success ) {
    osEventFlagsSet( ovenFanStatus, BIT( success ? FlagREIODone : FlagREIOError ) );
}

/// @brief Allocate the status event flag group and reset internal state.
void REInitModule( void ) {
    ovenFanStatus = osEventFlagsNew( NULL );

    memset( instances, 0, sizeof( instances ) );
    instances[ RotaryEncoder1 ].last_tick = osKernelGetTickCount();

    osEventFlagsClear( ovenFanStatus, 0xFFFFFF );
}

/// @brief Open a handle to a specific encoder instance.
///
/// On first call for a given ID: stores @p i2c in the instance. Subsequent calls
/// with the same ID return the existing instance.
///
/// @param[in] id   Encoder identifier.
/// @param[in] i2c  I2C bus handle returned by I2COpen().
/// @return Handle to the instance, or NULL if @p id is out of range.
RotaryEncoderRef REOpen( RotaryEncoderID id, I2CRef i2c ) {
    if ( id >= RotaryEncoderCount ) return NULL;
    RotaryEncoderPtr re = &instances[ id ];
    if ( re->i2c == NULL ) {
        re->id  = id;
        re->i2c = i2c;
    }
    return re;
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
    RotaryEncoderPtr re = &instances[ RotaryEncoder1 ];
    if ( re->i2c == NULL ) return;

    uint32_t flags = osEventFlagsGet( ovenFanStatus );

    if ( flags & BIT( FlagREIOError ) ) {
        osEventFlagsSet( ovenFanStatus, BIT( FlagREStatusHardwareFault ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        osEventFlagsClear( ovenFanStatus, BIT( FlagREIOError ) | BIT( FlagREIODone ) );
        re->state = REStateIdle;
    }

    switch ( re->state ) {
        case REStateIdle:
            osEventFlagsClear( ovenFanStatus, BIT( FlagREIODone ) | BIT( FlagREIOError ) );
            re->state = REStateReadAngle;
            if ( I2CReadAsync( re->i2c, kEncoderAddr, kRegAngle, I2C_MEMADD_SIZE_8BIT, (uint8_t*)re->buffer, 2, REI2CCallback ) != HAL_OK ) {
                re->state = REStateIdle;
            }
            break;

        case REStateReadAngle:
            if ( flags & BIT( FlagREIODone ) ) {
                uint16_t current_raw = ( ( uint16_t )re->buffer[ 0 ] << 8 ) | re->buffer[ 1 ];
                uint32_t now   = osKernelGetTickCount();
                uint32_t dt_ms = now - re->last_tick;

                if ( dt_ms >= 10 ) {
                    // Compute the shortest angular delta with wrap-around correction
                    int32_t delta = ( int32_t )current_raw - ( int32_t )re->last_raw;
                    if ( delta > 2048 )  delta -= 4096;
                    if ( delta < -2048 ) delta += 4096;

                    // Convert delta (4096 counts/rev) at dt_ms interval to RPM
                    re->current_velocity = ( Rpm )( ( delta * 60000 ) / ( ( int32_t )dt_ms * 4096 ) );
                    re->last_raw  = current_raw;
                    re->last_tick = now;
                }

                osEventFlagsClear( ovenFanStatus, BIT( FlagREIODone ) );
                re->state = REStateReadStatus;
                if ( I2CReadAsync( re->i2c, kEncoderAddr, kRegStatus, I2C_MEMADD_SIZE_8BIT, &re->status_reg, 1, REI2CCallback ) != HAL_OK ) {
                    re->state = REStateIdle;
                }
            }
            break;

        case REStateReadStatus:
            if ( flags & BIT( FlagREIODone ) ) {
                osEventFlagsClear( ovenFanStatus, BIT( FlagREIODone ) );
                re->state = REStateProcessing;
            }
            break;

        case REStateProcessing:
            break;
    }

    if ( re->state == REStateProcessing ) {
        uint32_t set   = BIT( FlagREStatusReady );
        uint32_t clear = BIT( FlagREStatusHardwareFault );

        // MD bit (bit 5): magnet detected. If clear, magnet is missing entirely.
        if ( !( re->status_reg & 0x20 ) ) {
            set |= BIT( FlagREStatusMagMissing );
            clear |= ( BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong ) );
        } else {
            clear |= BIT( FlagREStatusMagMissing );
            // MH bit (bit 3): magnet too far (AGC at max → weak)
            if ( re->status_reg & 0x08 ) {
                set |= BIT( FlagREStatusMagWeak );
                clear |= BIT( FlagREStatusMagStrong );
            // ML bit (bit 4): magnet too close (AGC at min → strong)
            } else if ( re->status_reg & 0x10 ) {
                set |= BIT( FlagREStatusMagStrong );
                clear |= BIT( FlagREStatusMagWeak );
            } else {
                clear |= ( BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong ) );
            }
        }

        if ( abs( re->current_velocity ) > kSpinningThresholdRpm ) {
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

        re->state = REStateIdle;
    }
}

/// @brief Return a handle to a previously opened encoder instance without re-initialising.
/// @param[in] id Encoder identifier.
/// @return Handle, or NULL if @p id has not been opened yet or is out of range.
RotaryEncoderRef REGetRef( RotaryEncoderID id ) {
    if ( id >= RotaryEncoderCount ) return NULL;
    return ( instances[ id ].i2c != NULL ) ? &instances[ id ] : NULL;
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
