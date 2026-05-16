#include <string.h>

#include "ThermistorI2C.h"
#include "I2CManager.h"

#define MCP3221_ADDR ( 0x4D << 1 )

typedef enum {
    TMStateIdle = 0,
    TMStateReadAdc,
    TMStateProcessing
} TMState;

struct ThermistorI2C {
    AdcRaw      latestRaw;
    TMState     state;
    uint8_t     buffer[ 2 ];
    bool        done;
    bool        error;
};

static struct ThermistorI2C instance;

// Initialises the I2C thermistor module.
void TMI2CInitModule( void ) {
    memset( &instance, 0, sizeof( struct ThermistorI2C ) );
    osEventFlagsClear( ThermistorHeatsinkStatusFlagsHandle, 0xFFFFFF );
}

// Callback for the I2CManager to signal completion or failure.
static void TMI2CCallback( bool success ) {
    if ( success ) {
        instance.done = true;
    } else {
        instance.error = true;
    }
}

// Provides a reference to the heatsink thermistor instance.
ThermistorI2CRef TMI2COpen( void ) {
    return &instance;
}

// Processes the I2C state machine and updates diagnostic flags.
void TMI2CProcess( void ) {
    if ( instance.error ) {
        osEventFlagsSet( ThermistorHeatsinkStatusFlagsHandle, BIT( FlagTMI2CStatusHardwareFault ) );
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
                instance.latestRaw = ( ( uint16_t )( instance.buffer[ 0 ] & 0x0F ) << 8 ) | instance.buffer[ 1 ];
                instance.state = TMStateProcessing;
            }
            break;

        case TMStateProcessing:
            break;
    }

    if ( instance.state == TMStateProcessing ) {
        uint32_t set = BIT( FlagTMI2CStatusReady );
        uint32_t clear = BIT( FlagTMI2CStatusHardwareFault );

        if ( instance.latestRaw >= 4090 ) {
            set |= BIT( FlagTMI2CStatusOpenCircuit );
        } else {
            clear |= BIT( FlagTMI2CStatusOpenCircuit );
        }

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

        osEventFlagsSet( ThermistorHeatsinkStatusFlagsHandle, set );
        osEventFlagsClear( ThermistorHeatsinkStatusFlagsHandle, clear );
        
        // Update global summary bit directly. Any local flag set (except Ready) trips the fault.
        if ( ( set & ~BIT( FlagTMI2CStatusReady ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
        } else {
            osEventFlagsClear( FaultFlagsHandle, BIT( FlagThermistorHeatsinkFault ) );
        }

        instance.state = TMStateIdle;
    }
}

// Returns the processed temperature in milli-degrees Celsius.
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor ) {
    if ( !thermistor ) return 0;
    
    int32_t val = ( int32_t )thermistor->latestRaw;
    return ( Temperature )( 120000 - ( val * 30 ) );
}

// Returns the raw 12-bit ADC value.
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor ) {
    return ( thermistor ) ? thermistor->latestRaw : 0;
}

// Returns the current bitmask of heatsink status and health flags.
uint32_t TMI2CGetStatus( void ) {
    return osEventFlagsGet( ThermistorHeatsinkStatusFlagsHandle );
}