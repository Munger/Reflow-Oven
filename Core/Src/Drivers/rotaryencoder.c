#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "RotaryEncoder.h"
#include "I2CManager.h"

#define ENCODER_ADDR ( 0x36 << 1 )
#define REG_ANGLE    0x0E
#define REG_STATUS   0x0B

#define RE_SPINNING_THRESHOLD_RPM 100

typedef enum {
    REStateIdle = 0,
    REStateReadAngle,
    REStateReadStatus,
    REStateProcessing
} REState;

typedef struct RotaryEncoder {
    uint16_t last_raw;
    uint32_t last_tick;
    Rpm      current_velocity;
    REState  state;
    uint8_t  buffer[ 2 ];
    uint8_t  status_reg;
    bool     done;
    bool     error;
} RotaryEncoder;

static RotaryEncoder instance;

// Callback for the I2CManager to signal completion or failure of asynchronous reads.
static void REI2CCallback( bool success ) {
    if ( success ) {
        instance.done = true;
    } else {
        instance.error = true;
    }
}

// Initialises the internal state of the encoder module.
void REInitModule( void ) {
    memset( &instance, 0, sizeof( RotaryEncoder ) );
    instance.last_tick = osKernelGetTickCount( );

    osEventFlagsClear( OvenFanStatusFlagsHandle, 0xFFFFFF );
}

// Provides a reference to the singleton encoder instance.
RotaryEncoderRef REOpen( void ) {
    return &instance;
}

// Performs the physical I2C state machine and updates velocity calculation.
void REProcess( void ) {
    if ( instance.error ) {
        osEventFlagsSet( OvenFanStatusFlagsHandle, BIT( FlagREStatusHardwareFault ) );
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
                uint32_t now = osKernelGetTickCount( );
                uint32_t dt_ms = now - instance.last_tick;

                if ( dt_ms >= 10 ) {
                    int32_t delta = ( int32_t )current_raw - ( int32_t )instance.last_raw;
                    if ( delta > 2048 )  delta -= 4096;
                    if ( delta < -2048 ) delta += 4096;

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

        if ( !( instance.status_reg & 0x20 ) ) {
            set |= BIT( FlagREStatusMagMissing );
            clear |= ( BIT( FlagREStatusMagWeak ) | BIT( FlagREStatusMagStrong ) );
        } else {
            clear |= BIT( FlagREStatusMagMissing );
            if ( instance.status_reg & 0x08 ) {
                set |= BIT( FlagREStatusMagWeak );
                clear |= BIT( FlagREStatusMagStrong );
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

        osEventFlagsSet( OvenFanStatusFlagsHandle, set );
        osEventFlagsClear( OvenFanStatusFlagsHandle, clear );

        // Raise global fault if anything other than Ready or Spinning is set
        if ( ( set & ~( BIT( FlagREStatusReady ) | BIT( FlagREStatusSpinning ) ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagOvenFanFault ) );
        }

        instance.state = REStateIdle;
    }
}

// Returns the most recently calculated velocity in RPM.
Rpm REGetVelocity( RotaryEncoderRef Encoder ) {
    return Encoder ? Encoder->current_velocity : 0;
}

// Returns the current bitmask of oven fan system status and health flags.
uint32_t REGetStatus( void ) {
    return osEventFlagsGet( OvenFanStatusFlagsHandle );
}