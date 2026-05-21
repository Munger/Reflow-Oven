/// @file RotaryEncoder.c
///
/// @brief AS5600 magnetic rotary encoder — I2C async state-machine implementation.
///
/// Implements a four-state polling loop: idle → read angle → read status → process.
/// The 12-bit angle difference between consecutive reads (with wrap-around correction)
/// is converted to RPM in REProcess(). All magnetic health bits (MD, MH, ML) are
/// decoded from the status register and mapped to instance status flags. Hardware
/// faults and global FaultFlagsHandle updates occur exclusively in REProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_ROTARY_ENCODER

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "event_groups.h"
#include "I2CAddress.h"
#include "RotaryEncoder.h"
#include "I2CManager.h"

static const uint16_t kEncoderAddr = (uint16_t)I2CAddrAS5600 << 1;

// ============================================================================
// AS5600 register addresses
// ============================================================================

static const uint8_t kRegAngle  = 0x0EU; ///< 12-bit raw angle (MSB at 0x0E, LSB at 0x0F)
static const uint8_t kRegStatus = 0x0BU; ///< Status register: bit5=MD, bit4=ML (too close), bit3=MH (too far)

/// @brief Velocity above this threshold sets FlagREStatusSpinning.
static const Rpm kSpinningThresholdRpm = 100;

/// @brief Internal state machine states for the async I2C polling loop.
typedef enum {
    REStateIdle = 0,    ///< Waiting to start a new angle read cycle
    REStateReadAngle,   ///< Waiting for 2-byte angle register read to complete
    REStateReadStatus,  ///< Waiting for 1-byte status register read to complete
    REStateProcessing   ///< All reads complete; computing velocity and updating flags
} REState;

/// @brief Full internal state of one encoder instance.
typedef struct RotaryEncoderInstance {
    osEventFlagsId_t statusHandle;       ///< Per-instance status and diagnostic flags
    StaticEventGroup_t statusBuffer;     ///< Storage backing statusHandle (no-heap allocation)
    I2CRef           i2c;                ///< I2C bus handle acquired at REOpen()
    uint16_t         last_raw;           ///< Previous 12-bit angle value for delta computation
    uint32_t         last_tick;          ///< Kernel tick at the time of the last angle sample
    Rpm              current_velocity;   ///< Most recently computed velocity in RPM
    REState          state;              ///< Current state machine position
    volatile uint8_t buffer[ 2 ];        ///< Raw bytes returned by the last I2C read (ISR-written)
    uint8_t          status_reg;         ///< Last value read from REG_STATUS
} RotaryEncoderInstance, *RotaryEncoderInstancePtr;

/// @brief All encoder instances — indexed by RotaryEncoderID.
static RotaryEncoderInstance instances[ RotaryEncoderCount ];

/// @brief Instance currently awaiting an async I2C completion callback.
static volatile RotaryEncoderInstancePtr s_active = NULL;

/// @brief I2CManager completion callback — sets FlagREIODone or FlagREIOError on the active instance.
///
/// @param[in] success true if the I2C read completed without error.
/// @warning Called from HAL ISR context. osEventFlagsSet() only — no blocking API.
static void REI2CCallback( bool success ) {
    if ( s_active ) {
        osEventFlagsSet( s_active->statusHandle, BIT( success ? FlagREIODone : FlagREIOError ) );
    }
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate per-instance status event flag groups and initialise tick counters.
void REInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    for ( uint8_t i = 0; i < RotaryEncoderCount; i++ ) {
        instances[ i ].statusHandle = osEventFlagsNew( &(osEventFlagsAttr_t){ .cb_mem = &instances[ i ].statusBuffer, .cb_size = sizeof( StaticEventGroup_t ) } );
        instances[ i ].last_tick    = osKernelGetTickCount();
    }
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagRotaryEncoderReady ) );
}

/// @brief Open a handle to a specific encoder instance.
///
/// On first call for a given ID: stores @p i2c in the instance. Subsequent calls
/// with the same ID return the existing instance.
RotaryEncoderRef REOpen( RotaryEncoderID id, I2CRef i2c ) {
    if ( id >= RotaryEncoderCount ) return NULL;
    RotaryEncoderInstancePtr re = &instances[ id ];
    if ( re->i2c == NULL ) {
        re->i2c = i2c;
        osEventFlagsSet( re->statusHandle, BIT( FlagREStatusReady ) );
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
///      updates instance statusHandle and FaultFlagsHandle, resets to Idle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void REProcess( void ) {
    for ( uint8_t i = 0; i < RotaryEncoderCount; i++ ) {
        RotaryEncoderInstancePtr re = &instances[ i ];
        if ( re->i2c == NULL ) continue;

        uint32_t flags = osEventFlagsGet( re->statusHandle );

        if ( flags & BIT( FlagREIOError ) ) {
            osEventFlagsSet( re->statusHandle, BIT( FlagREStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
            osEventFlagsClear( re->statusHandle, BIT( FlagREIOError ) | BIT( FlagREIODone ) );
            re->state = REStateIdle;
        }

        switch ( re->state ) {
            case REStateIdle:
                osEventFlagsClear( re->statusHandle, BIT( FlagREIODone ) | BIT( FlagREIOError ) );
                re->state = REStateReadAngle;
                s_active  = re;
                if ( I2CReadAsync( re->i2c, kEncoderAddr, kRegAngle, I2C_MEMADD_SIZE_8BIT,
                                   (uint8_t*)re->buffer, 2, REI2CCallback ) != HAL_OK ) {
                    s_active  = NULL;
                    re->state = REStateIdle;
                }
                break;

            case REStateReadAngle:
                if ( flags & BIT( FlagREIODone ) ) {
                    uint16_t current_raw = ( ( uint16_t )re->buffer[ 0 ] << 8 ) | re->buffer[ 1 ];
                    uint32_t now   = osKernelGetTickCount();
                    uint32_t dt_ms = now - re->last_tick;

                    if ( dt_ms >= 10 ) {
                        int32_t delta = ( int32_t )current_raw - ( int32_t )re->last_raw;
                        if ( delta > 2048 )  delta -= 4096;
                        if ( delta < -2048 ) delta += 4096;
                        re->current_velocity = ( Rpm )( ( delta * 60000 ) / ( ( int32_t )dt_ms * 4096 ) );
                        re->last_raw  = current_raw;
                        re->last_tick = now;
                    }

                    osEventFlagsClear( re->statusHandle, BIT( FlagREIODone ) );
                    re->state = REStateReadStatus;
                    s_active  = re;
                    if ( I2CReadAsync( re->i2c, kEncoderAddr, kRegStatus, I2C_MEMADD_SIZE_8BIT,
                                       &re->status_reg, 1, REI2CCallback ) != HAL_OK ) {
                        s_active  = NULL;
                        re->state = REStateIdle;
                    }
                }
                break;

            case REStateReadStatus:
                if ( flags & BIT( FlagREIODone ) ) {
                    osEventFlagsClear( re->statusHandle, BIT( FlagREIODone ) );
                    s_active  = NULL;
                    re->state = REStateProcessing;
                }
                break;

            case REStateProcessing:
                break;
        }

        if ( re->state == REStateProcessing ) {
            uint32_t set   = BIT( FlagREStatusReady );
            uint32_t clear = BIT( FlagREStatusHardwareFault );

            if ( !( re->status_reg & 0x20 ) ) {
                set   |= BIT( FlagREStatusMagMissing );
                clear |= BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong );
            } else {
                clear |= BIT( FlagREStatusMagMissing );
                if ( re->status_reg & 0x08 ) {
                    set   |= BIT( FlagREStatusMagWeak );
                    clear |= BIT( FlagREStatusMagStrong );
                } else if ( re->status_reg & 0x10 ) {
                    set   |= BIT( FlagREStatusMagStrong );
                    clear |= BIT( FlagREStatusMagWeak );
                } else {
                    clear |= BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong );
                }
            }

            if ( abs( re->current_velocity ) > kSpinningThresholdRpm ) {
                set   |= BIT( FlagREStatusSpinning );
            } else {
                clear |= BIT( FlagREStatusSpinning );
            }

            osEventFlagsSet( re->statusHandle, set );
            osEventFlagsClear( re->statusHandle, clear );

            if ( ( set & ~( BIT( FlagREStatusReady ) | BIT( FlagREStatusSpinning ) ) ) != 0 ) {
                osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
            } else {
                osEventFlagsClear( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
            }

            re->state = REStateIdle;
        }
    }
}

/// @brief Return a handle to a previously opened encoder instance without re-initialising.
RotaryEncoderRef REGetRef( RotaryEncoderID id ) {
    if ( id >= RotaryEncoderCount ) return NULL;
    return ( instances[ id ].i2c != NULL ) ? &instances[ id ] : NULL;
}

/// @brief Return the most recently computed velocity.
Rpm REGetVelocity( RotaryEncoderRef encoder ) {
    return encoder ? ( (RotaryEncoderInstancePtr)encoder )->current_velocity : 0;
}

/// @brief Return the full status bitmask for an encoder instance.
uint32_t REGetStatus( RotaryEncoderRef encoder ) {
    if ( !encoder ) return 0;
    return osEventFlagsGet( ( (RotaryEncoderInstancePtr)encoder )->statusHandle );
}

#endif // FEATURE_ROTARY_ENCODER
