#include <string.h>
#include <stdlib.h>

#include "thermistor.h"
#include "adc.h"

#define ADC_RANK_COUNT 7
#define TABLE_SIZE     32

AdcRaw AdcDataBuffer[ ADC_RANK_COUNT ];

static ADC_HandleTypeDef* pAdcHandle = NULL;

static const uint16_t AdcStepShift = 7;
static const uint16_t AdcStepMask  = 127;
static const uint16_t FaultMarginLow  = 50;
static const uint16_t FaultMarginHigh = 4045;

static const Temperature CjtTable[] = {
    -40000, -25000, -14500, -6000,  1000,   7200,   13000,  18300, 
    23500,  28400,  33200,  37800,  42500,  47200,  51900,  56700, 
    61700,  66800,  72200,  77900,  84000,  90500,  97600,  105500, 
    114300, 124300, 136100, 150300, 168500, 193000, 230000, 280000,
    330000
};

static const Temperature OvenTable[] = {
    300000, 285000, 270000, 256000, 242000, 229000, 216000, 204000,
    192000, 180000, 169000, 158000, 147000, 136000, 126000, 116000,
    106000, 96000,  86000,  77000,  68000,  59000,  50000,  42000,
    34000,  26000,  19000,  13000,  8000,   4000,   2000,   1000,
    0
};

typedef struct Thermistor {
    ThermistorID     id;
    uint8_t          bufferIndex;
    osEventFlagsId_t flags;
    uint32_t         globalFaultBit;
} Thermistor;

static Thermistor instances[ ThermistorCount ];

void TMInitModule( void ) {
    pAdcHandle = &ADC1Handle;
    memset( instances, 0, sizeof( instances ) );

    instances[ 0 ] = ( Thermistor ){ 
        .id = ThermistorCJT1, 
        .bufferIndex = 0, 
        .flags = ThermistorCJT1StatusFlagsHandle,
        .globalFaultBit = BIT( FlagThermistorCJT1Fault )
    };

    instances[ 1 ] = ( Thermistor ){ 
        .id = ThermistorCJT2, 
        .bufferIndex = 1, 
        .flags = ThermistorCJT2StatusFlagsHandle,
        .globalFaultBit = BIT( FlagThermistorCJT2Fault ) 
    };

    instances[ 2 ] = ( Thermistor ){ 
        .id = ThermistorOven, 
        .bufferIndex = 2, 
        .flags = ThermistorOvenStatusFlagsHandle,
        .globalFaultBit = BIT( FlagThermistorOvenFault )
    };

    osEventFlagsClear( ThermistorCJT1StatusFlagsHandle, 0xFFFFFF );
    osEventFlagsClear( ThermistorCJT2StatusFlagsHandle, 0xFFFFFF );
    osEventFlagsClear( ThermistorOvenStatusFlagsHandle, 0xFFFFFF );

    HAL_ADCEx_Calibration_Start( pAdcHandle );
    HAL_ADC_Start_DMA( pAdcHandle, ( uint32_t* )AdcDataBuffer, pAdcHandle->Init.NbrOfConversion );
}

ThermistorRef TMOpen( ThermistorID thermistorID ) {
    if ( thermistorID >= ThermistorCount ) return NULL;
    return &instances[ thermistorID ];
}

Temperature TMGetTemperature( ThermistorRef thermistor ) {
    if ( !thermistor ) return 0;

    AdcRaw raw = AdcDataBuffer[ thermistor->bufferIndex ];
    const Temperature* table = ( thermistor->id == ThermistorOven ) ? OvenTable : CjtTable;

    uint16_t index = raw >> AdcStepShift;
    if ( index >= TABLE_SIZE ) index = TABLE_SIZE - 1;

    uint16_t remainder = raw & AdcStepMask;
    Temperature y0 = table[ index ];
    Temperature y1 = table[ index + 1 ];

    int32_t deltaY = ( int32_t )y1 - ( int32_t )y0;
    return y0 + ( ( deltaY * ( int32_t )remainder ) >> AdcStepShift );
}

void TMProcess( void ) {
    bool hwError = ( HAL_ADC_GetState( pAdcHandle ) & HAL_ADC_STATE_ERROR );

    for ( uint8_t i = 0; i < ThermistorCount; i++ ) {
        Thermistor* tm = &instances[ i ];
        AdcRaw raw = AdcDataBuffer[ tm->bufferIndex ];
        
        uint32_t set = BIT( FlagTMStatusReady );
        uint32_t clear = BIT( FlagTMStatusHardwareFault ) | BIT( FlagTMStatusOpenCircuit ) | BIT( FlagTMStatusShortCircuit );

        if ( hwError ) {
            set |= BIT( FlagTMStatusHardwareFault );
        } else {
            // Fault logic for the divider networks
            if ( tm->id == ThermistorOven ) {
                if ( raw >= FaultMarginHigh ) set |= BIT( FlagTMStatusOpenCircuit );
                if ( raw <= FaultMarginLow )  set |= BIT( FlagTMStatusShortCircuit );
            } else {
                if ( raw <= FaultMarginLow )  set |= BIT( FlagTMStatusOpenCircuit );
                if ( raw >= FaultMarginHigh ) set |= BIT( FlagTMStatusShortCircuit );
            }
        }

        Temperature temp = TMGetTemperature( tm );
        
        // High/Low Plausibility Limits
        if ( temp > 280000 ) {
            set |= BIT( FlagTMStatusHighTemp );
        } else {
            clear |= BIT( FlagTMStatusHighTemp );
        }

        if ( temp < -30000 ) {
            set |= BIT( FlagTMStatusLowTemp );
        } else {
            clear |= BIT( FlagTMStatusLowTemp );
        }

        osEventFlagsSet( tm->flags, set );
        osEventFlagsClear( tm->flags, clear );

        // Map to specific global fault bit
        if ( ( set & ~BIT( FlagTMStatusReady ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, tm->globalFaultBit );
        } else {
            osEventFlagsClear( FaultFlagsHandle, tm->globalFaultBit );
        }
    }
}

uint32_t TMGetStatus( ThermistorRef thermistor ) {
    return ( thermistor ) ? osEventFlagsGet( thermistor->flags ) : 0;
}