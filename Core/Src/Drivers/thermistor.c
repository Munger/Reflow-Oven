#include <string.h>
#include "thermistor.h"

// Private DMA buffer hidden from the rest of the application.
// Matches the 7 Ranks configured in CubeMX.
static AdcRaw AdcDataBuffer[ 7 ];

static ADC_HandleTypeDef* pAdcHandle = NULL;

// ADC resolution constants for bitwise optimization (128 = 2^7)
static const uint16_t AdcStepShift = 7;
static const uint16_t AdcStepMask  = 127;

// Health check thresholds
static const uint16_t FaultMarginLow  = 50;
static const uint16_t FaultMarginHigh = 4045;

// Table for CJT1/CJT2 (B57861S0103F040 10k NTC, 5.6k Pull-down)
// Configuration: VCC -> NTC -> [ADC] -> 5.6k -> GND
static const Temperature CjtTable[] = {
    -40000, -25000, -14500, -6000,  1000,   7200,   13000,  18300, 
    23500,  28400,  33200,  37800,  42500,  47200,  51900,  56700, 
    61700,  66800,  72200,  77900,  84000,  90500,  97600,  105500, 
    114300, 124300, 136100, 150300, 168500, 193000, 230000, 280000,
    330000
};

// Table for Oven (300C Oven NTC, 7.5k Pull-up)
// Configuration: VCC -> 7.5k -> [ADC] -> NTC -> GND
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
    ThermistorStatus status;
    Temperature      lastTemp;
} Thermistor;

static Thermistor instances[ 3 ];

void TMInitModule( ADC_HandleTypeDef* hadc ) {
    if ( !hadc ) return;

    pAdcHandle = hadc;
    memset( instances, 0, sizeof( instances ) );

    // Map Thermistor IDs to DMA buffer indices based on ADC Rank order.
    instances[ 0 ].id = ThermistorCJT1;
    instances[ 0 ].bufferIndex = 0; // Rank 1: IN2

    instances[ 1 ].id = ThermistorCJT2;
    instances[ 1 ].bufferIndex = 1; // Rank 2: IN3

    instances[ 2 ].id = ThermistorOven;
    instances[ 2 ].bufferIndex = 2; // Rank 3: IN8

    for ( uint8_t i = 0; i < 3; i++ ) {
        instances[ i ].status = TMStatusNoResponse;
    }

    // Start hardware sequence directly from the driver.
    HAL_ADCEx_Calibration_Start( pAdcHandle );
    HAL_ADC_Start_DMA( pAdcHandle, (uint32_t*)AdcDataBuffer, pAdcHandle->Init.NbrOfConversion );
}

ThermistorRef TMOpen( ThermistorID thermistorID ) {
    for ( uint8_t i = 0; i < 3; i++ ) {
        if ( instances[ i ].id == thermistorID ) return &instances[ i ];
    }
    return NULL;
}

Temperature TMGetTemperature( ThermistorRef thermistor ) {
    if ( !thermistor ) return 0;

    AdcRaw raw = AdcDataBuffer[ thermistor->bufferIndex ];
    const Temperature* table;

    // Logic for High-Side NTC (CJTs)
    if ( thermistor->id != ThermistorOven ) {
        if ( raw <= FaultMarginLow ) {
            thermistor->status = TMStatusOpenCircuit;
            return -999000;
        }
        if ( raw >= FaultMarginHigh ) {
            thermistor->status = TMStatusShortToGnd;
            return 999000;
        }
        table = CjtTable;
    }
    // Logic for Low-Side NTC (Oven)
    else {
        if ( raw >= FaultMarginHigh ) {
            thermistor->status = TMStatusOpenCircuit;
            return -999000;
        }
        if ( raw <= FaultMarginLow ) {
            thermistor->status = TMStatusShortToGnd;
            return 999000;
        }
        table = OvenTable;
    }

    thermistor->status = TMStatusOk;

    // Linear interpolation using bitwise logic to avoid costly %.
    uint16_t index = raw >> AdcStepShift;
    if ( index >= 32 ) index = 31;

    uint16_t remainder = raw & AdcStepMask;
    Temperature y0 = table[ index ];
    Temperature y1 = table[ index + 1 ];

    int32_t deltaY = (int32_t)y1 - (int32_t)y0;
    
    // Scale interpolation without division helper calls.
    thermistor->lastTemp = y0 + ( ( deltaY * (int32_t)remainder ) >> AdcStepShift );

    return thermistor->lastTemp;
}

ThermistorStatus TMGetStatus( ThermistorRef thermistor ) {
    if ( !thermistor ) return TMStatusOutOfRange;
    return thermistor->status;
}