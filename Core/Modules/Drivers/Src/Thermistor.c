/// @file Thermistor.c
///
/// @brief NTC thermistor driver — ADC DMA buffer linearisation implementation.
///
/// Reads three NTC channels from the DMA-populated AdcDataBuffer and applies
/// piecewise linear interpolation against a 33-entry lookup table. The oven
/// thermistor uses an inverted divider network (table is descending), while the
/// two CJT channels use an ascending table. Per-instance event flag groups are
/// created in TMInitModule() and stored inside the Thermistor struct; they are
/// not exposed as extern handles.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>
#include <stdlib.h>

#include "Thermistor.h"
#include "adc.h"

/// @brief Total number of ADC channels in the DMA conversion sequence.
enum { kAdcRankCount = 7 };

/// @brief Number of intervals in each linearisation lookup table (33 entries total).
enum { kTableSize = 32 };

/// @brief DMA-populated ADC sample buffer, shared with mcu.c for internal channels.
/// Indices 0–2 are thermistor channels; 4–6 are used by the MCU driver.
AdcRaw AdcDataBuffer[ kAdcRankCount ];

/// @brief ADC handle pointer used to start DMA and check peripheral state.
static ADC_HandleTypeDef* pAdcHandle = NULL;

/// @brief Number of 12-bit ADC counts per lookup table step (4096 / 32 = 128).
static const uint16_t kAdcStepShift = 7;

/// @brief Bitmask for the fractional count within a single lookup table step.
static const uint16_t kAdcStepMask  = 127;

/// @brief ADC values within this distance from the supply rail are treated as open-circuit.
static const uint16_t kFaultMarginLow  = 50;

/// @brief ADC values within this distance from the rail opposite to normal are treated as short.
static const uint16_t kFaultMarginHigh = 4045;

/// @brief Piecewise linearisation table for the CJT (cold-junction) NTC thermistors.
/// 33 entries covering the full 12-bit ADC range (ascending ADC → increasing temperature).
/// Values are in milli-degrees Celsius.
static const Temperature CjtTable[] = {
    -40000, -25000, -14500, -6000,  1000,   7200,   13000,  18300,
    23500,  28400,  33200,  37800,  42500,  47200,  51900,  56700,
    61700,  66800,  72200,  77900,  84000,  90500,  97600,  105500,
    114300, 124300, 136100, 150300, 168500, 193000, 230000, 280000,
    330000
};

/// @brief Piecewise linearisation table for the oven cavity NTC thermistor.
/// Descending temperature because the oven network uses an inverted divider.
/// Values are in milli-degrees Celsius.
static const Temperature OvenTable[] = {
    300000, 285000, 270000, 256000, 242000, 229000, 216000, 204000,
    192000, 180000, 169000, 158000, 147000, 136000, 126000, 116000,
    106000, 96000,  86000,  77000,  68000,  59000,  50000,  42000,
    34000,  26000,  19000,  13000,  8000,   4000,   2000,   1000,
    0
};

/// @brief Internal per-channel thermistor state.
typedef struct Thermistor {
    ThermistorID     id;             ///< Channel identifier
    uint8_t          bufferIndex;    ///< Index into AdcDataBuffer for this channel
    osEventFlagsId_t flags;          ///< Private event flag group for this instance
    uint32_t         globalFaultBit; ///< BIT(FlagXxx) to set/clear in FaultFlagsHandle
} Thermistor;

/// @brief Thermistor instance array indexed by ThermistorID.
static Thermistor instances[ ThermistorCount ];

/// @brief Initialise the NTC module, create per-instance flag groups, and start ADC DMA.
///
/// Each instance gets its own osEventFlagsId_t created via osEventFlagsNew().
/// The ADC calibration is run and DMA conversion is started. DeviceStatusFlagsHandle
/// signals are intentionally not set here — thermistors are internal CJT sensors
/// that are considered implicitly ready when ADC DMA is running.
void TMInitModule( void ) {
    pAdcHandle = &ADC1Handle;
    memset( instances, 0, sizeof( instances ) );

    instances[ 0 ] = ( Thermistor ){
        .id             = ThermistorCJT1,
        .bufferIndex    = 0,
        .flags          = osEventFlagsNew( NULL ),
        .globalFaultBit = BIT( FlagThermistorCJT1Fault )
    };

    instances[ 1 ] = ( Thermistor ){
        .id             = ThermistorCJT2,
        .bufferIndex    = 1,
        .flags          = osEventFlagsNew( NULL ),
        .globalFaultBit = BIT( FlagThermistorCJT2Fault )
    };

    instances[ 2 ] = ( Thermistor ){
        .id             = ThermistorOven,
        .bufferIndex    = 2,
        .flags          = osEventFlagsNew( NULL ),
        .globalFaultBit = BIT( FlagThermistorOvenFault )
    };

    for ( uint8_t i = 0; i < ThermistorCount; i++ ) {
        if ( instances[ i ].flags != NULL ) {
            osEventFlagsClear( instances[ i ].flags, 0xFFFFFF );
        }
    }

    HAL_ADCEx_Calibration_Start( pAdcHandle );
    HAL_ADC_Start_DMA( pAdcHandle, ( uint32_t* )AdcDataBuffer, pAdcHandle->Init.NbrOfConversion );
}

/// @brief Open a handle to a specific thermistor channel.
/// @param[in] thermistorID Channel identifier.
/// @return Pointer to the Thermistor instance, or NULL if out of range.
ThermistorRef TMOpen( ThermistorID thermistorID ) {
    if ( thermistorID >= ThermistorCount ) return NULL;
    return &instances[ thermistorID ];
}

/// @brief Interpolate the temperature from the current ADC DMA reading.
///
/// Reads the raw 12-bit ADC value directly from AdcDataBuffer, selects the
/// appropriate lookup table (oven vs CJT), computes the table index and fractional
/// remainder, then applies linear interpolation between adjacent entries.
///
/// @param[in] thermistor Handle returned by TMOpen().
/// @return Interpolated temperature in milli-degrees Celsius; 0 if @p thermistor is NULL.
/// @note Pure computation — no flag side-effects. Safe to call from any task context.
Temperature TMGetTemperature( ThermistorRef thermistor ) {
    if ( !thermistor ) return 0;

    AdcRaw raw = AdcDataBuffer[ thermistor->bufferIndex ];
    const Temperature* table = ( thermistor->id == ThermistorOven ) ? OvenTable : CjtTable;

    uint16_t index = raw >> kAdcStepShift;
    if ( index >= kTableSize ) index = kTableSize - 1;

    uint16_t remainder = raw & kAdcStepMask;
    Temperature y0 = table[ index ];
    Temperature y1 = table[ index + 1 ];

    int32_t deltaY = ( int32_t )y1 - ( int32_t )y0;
    return y0 + ( ( deltaY * ( int32_t )remainder ) >> kAdcStepShift );
}

/// @brief Evaluate fault thresholds for all channels and update status and global fault flags.
///
/// For each instance: checks the ADC state for hardware error, evaluates open/short
/// circuit margins (note the polarity is inverted for the oven channel), checks
/// temperature plausibility limits, and sets/clears the appropriate per-instance flags.
/// Propagates any active fault to the FaultFlagsHandle global bus.
///
/// @warning Must be called from task context — reads HAL_ADC_GetState().
void TMProcess( void ) {
    bool hwError = ( HAL_ADC_GetState( pAdcHandle ) & HAL_ADC_STATE_ERROR );

    for ( uint8_t i = 0; i < ThermistorCount; i++ ) {
        Thermistor* tm = &instances[ i ];
        AdcRaw raw = AdcDataBuffer[ tm->bufferIndex ];

        uint32_t set   = BIT( FlagTMStatusReady );
        uint32_t clear = BIT( FlagTMStatusHardwareFault ) | BIT( FlagTMStatusOpenCircuit ) | BIT( FlagTMStatusShortCircuit );

        if ( hwError ) {
            set |= BIT( FlagTMStatusHardwareFault );
        } else {
            // Fault logic accounts for the inverted divider of the oven network.
            // CJT channels: high ADC → open, low ADC → short.
            // Oven channel: high ADC → short, low ADC → open.
            if ( tm->id == ThermistorOven ) {
                if ( raw >= kFaultMarginHigh ) set |= BIT( FlagTMStatusOpenCircuit );
                if ( raw <= kFaultMarginLow )  set |= BIT( FlagTMStatusShortCircuit );
            } else {
                if ( raw <= kFaultMarginLow )  set |= BIT( FlagTMStatusOpenCircuit );
                if ( raw >= kFaultMarginHigh ) set |= BIT( FlagTMStatusShortCircuit );
            }
        }

        Temperature temp = TMGetTemperature( tm );

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

        // Any flag other than Ready trips the per-channel global fault bit
        if ( ( set & ~BIT( FlagTMStatusReady ) ) != 0 ) {
            osEventFlagsSet( FaultFlagsHandle, tm->globalFaultBit );
        } else {
            osEventFlagsClear( FaultFlagsHandle, tm->globalFaultBit );
        }
    }
}

/// @brief Return the current status bitmask for a specific thermistor instance.
/// @param[in] thermistor Handle returned by TMOpen().
/// @return Bitmask of TMStatusBit flags; 0 if @p thermistor is NULL.
/// @note Safe to call from any task context without blocking.
uint32_t TMGetStatus( ThermistorRef thermistor ) {
    return ( thermistor ) ? osEventFlagsGet( thermistor->flags ) : 0;
}
