#include <string.h>

#include "main.h"
#include "triac.h"

extern TIM_HandleTypeDef htim16;

typedef struct TriacDevice {
    const TriacID       id;
    const uint16_t      pin;
    GPIO_TypeDef* const port;
    osEventFlagsId_t    flags;
    TriacDriveParams    currentParams;
    uint8_t             burstCounter;
} TriacDevice, *TriacDevicePtr;

// Hardware mapping: Designated initialisers pin the data to the enum value, not the array index
static TriacDevice devices[ TriacCount ] = {
    [ TriacHeaterTop ]      = { .id = TriacHeaterTop,       .pin = HTR_TOP_EN_N_Pin,    .port = HTR_TOP_EN_N_GPIO_Port  },
    [ TriacHeaterRear ]     = { .id = TriacHeaterRear,      .pin = HTR_REAR_EN_N_Pin,   .port = HTR_REAR_EN_N_GPIO_Port },
    [ TriacHeaterBottom ]   = { .id = TriacHeaterBottom,    .pin = HTR_BOT_EN_N_Pin,    .port = HTR_BOT_EN_N_GPIO_Port  },
    [ TriacOvenFan ]        = { .id = TriacOvenFan,         .pin = OVEN_FAN_EN_N_Pin,   .port = OVEN_FAN_EN_N_GPIO_Port },
    [ TriacLight ]          = { .id = TriacLight,           .pin = LIGHT_EN_N_Pin,      .port = LIGHT_EN_N_GPIO_Port    }
};

static uint8_t  activeSequence[ TriacCount ];
static uint8_t  sequenceCount = 0;
static uint8_t  currentSeqIndex = 0;
static bool     isPulseWidthDelay = false;
static uint32_t lastZcdTick = 0;

static void TriacTimerCallback( TIM_HandleTypeDef* htim );

// Maps OS handles to the device array and sets the physical pins to their default inactive state
void TriacInitModule( void ) {
    // By using designated initialisers here, we ensure handles are paired with the correct hardware structure
    osEventFlagsId_t handles[ TriacCount ] = {
        [ TriacHeaterTop ]    = TriacHTopStatusFlagsHandle,
        [ TriacHeaterRear ]   = TriacHRearStatusFlagsHandle,
        [ TriacHeaterBottom ] = TriacHBotStatusFlagsHandle,
        [ TriacOvenFan ]      = TriacFanStatusFlagsHandle,
        [ TriacLight ]        = TriacLightStatusFlagsHandle
    };

    for ( int i = 0; i < TriacCount; i++ ) {
        devices[ i ].flags = handles[ i ];
        // TRIAC gates are Active-Low; SET = Off
        HAL_GPIO_WritePin( devices[ i ].port, devices[ i ].pin, GPIO_PIN_SET );
        osEventFlagsSet( devices[ i ].flags, 1 << FlagTriacStatusReady );
    }

    HAL_TIM_RegisterCallback( &htim16, HAL_TIM_PERIOD_ELAPSED_CB_ID, TriacTimerCallback );
}

// Fetches a handle for the requested TRIAC channel
TriacRef TriacOpen( TriacID id ) {
    if ( id >= TriacCount ) return NULL;
    return &devices[ id ];
}

// Direct full-power control bypassing sequenced phase-angle logic
void TriacOn( TriacRef triac ) {
    if ( !triac ) return;
    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusPhaseAngle );
    HAL_GPIO_WritePin( triac->port, triac->pin, GPIO_PIN_RESET );
    osEventFlagsSet( triac->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
}

// Shuts down the TRIAC and removes it from sequenced control
void TriacOff( TriacRef triac ) {
    if ( !triac ) return;
    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusPhaseAngle );
    HAL_GPIO_WritePin( triac->port, triac->pin, GPIO_PIN_SET );
    osEventFlagsClear( triac->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
}

// Configures parameters and asserts local/global faults immediately if they are out of range
void TriacRun( TriacRef triac, TriacDriveParams params ) {
    if ( !triac ) return;

    // Safety check: 10ms is the absolute limit for a 50Hz half-cycle
    if ( params.phaseDelayUs > 10000 || params.burstOn > params.burstWindow ) {
        osEventFlagsSet( triac->flags, 1 << FlagTriacStatusConfigError );
        osEventFlagsSet( FaultFlagsHandle, 1 << FlagTriacFault );
        return;
    }

    osEventFlagsClear( triac->flags, 1 << FlagTriacStatusConfigError );
    memcpy( &triac->currentParams, &params, sizeof( TriacDriveParams ) );
    triac->burstCounter = 0;
    osEventFlagsSet( triac->flags, 1 << FlagTriacStatusPhaseAngle );
}

// Returns the current status flags for a specific TRIAC device
uint32_t TriacGetStatus( TriacRef triac ) {
    if ( !triac ) return 0;
    return osEventFlagsGet( triac->flags );
}

// Zero-cross interrupt handler for synchronising phase-angle firing and burst windows
void ZCDHandler( uint16_t GPIO_Pin ) {
    sequenceCount = 0;
    currentSeqIndex = 0;
    isPulseWidthDelay = false;
    lastZcdTick = osKernelGetTickCount();

    // Iterate all devices to update burst counters and identify channels needing a pulse
    for ( int i = 0; i < TriacCount; i++ ) {
        TriacDevice* dev = &devices[ i ];
        uint32_t flags = osEventFlagsGet( dev->flags );

        // Clear transient sequencer flags at the start of every half-cycle
        osEventFlagsClear( dev->flags, ( 1 << FlagTriacStatusZCDLost ) | 
                                       ( 1 << FlagTriacStatusPulsePending ) | 
                                       ( 1 << FlagTriacStatusPulseActive ) );

        // Skip devices in manual mode or otherwise not under sequenced control
        if ( !( flags & ( 1 << FlagTriacStatusPhaseAngle ) ) ) continue;

        // Check if current half-cycle falls within the 'On' portion of the burst window
        if ( dev->burstCounter < dev->currentParams.burstOn ) {
            osEventFlagsSet( dev->flags, 1 << FlagTriacStatusActive );
            
            // For Alpha=0 (Full power), fire immediately to avoid interrupt jitter
            if ( dev->currentParams.phaseDelayUs == 0 ) {
                HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_RESET );
                osEventFlagsSet( dev->flags, 1 << FlagTriacStatusGateOpen );
            } else {
                // Schedule for timed pulse sequence
                osEventFlagsSet( dev->flags, 1 << FlagTriacStatusPulsePending );
                activeSequence[ sequenceCount++ ] = i;
            }
        } else {
            // Burst window is 'Off': ensure pin is high and flags are clear
            HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_SET );
            osEventFlagsClear( dev->flags, ( 1 << FlagTriacStatusActive ) | ( 1 << FlagTriacStatusGateOpen ) );
        }

        // Cycle the burst window counter
        if ( ++dev->burstCounter >= dev->currentParams.burstWindow ) {
            dev->burstCounter = 0;
        }
    }

    // Sort the firing order based on phase delay (Earliest firing first)
    for ( int i = 1; i < sequenceCount; i++ ) {
        uint8_t val = activeSequence[ i ];
        int j = i - 1;
        while ( j >= 0 && devices[ activeSequence[ j ] ].currentParams.phaseDelayUs >
                          devices[ val ].currentParams.phaseDelayUs ) {
            activeSequence[ j + 1 ] = activeSequence[ j ];
            j--;
        }
        activeSequence[ j + 1 ] = val;
    }

    // Prime the hardware timer with the first delay and latch it immediately
    if ( sequenceCount > 0 ) {
        __HAL_TIM_SET_AUTORELOAD( &htim16, devices[ activeSequence[ 0 ] ].currentParams.phaseDelayUs );
        HAL_TIM_GenerateEvent( &htim16, TIM_EVENTSOURCE_UPDATE ); // Latches the new ARR
        HAL_TIM_Base_Start_IT( &htim16 );
    } else {
        HAL_TIM_Base_Stop_IT( &htim16 );
    }
}

// Timer ISR: Manages the 'On' pulse width (100us) and the 'Gap' between different TRIAC delays
static void TriacTimerCallback( TIM_HandleTypeDef* htim ) {
    // PHASE 1: Pulse Initiation
    if ( !isPulseWidthDelay ) {
        TriacDevicePtr dev = &devices[ activeSequence[ currentSeqIndex ] ];
        
        HAL_GPIO_WritePin( dev->port, dev->pin, GPIO_PIN_RESET );
        // Mark as conducting by sequencer; PulseActive prevents accidental manual shutdown
        osEventFlagsSet( dev->flags, ( 1 << FlagTriacStatusGateOpen ) | ( 1 << FlagTriacStatusPulseActive ) ); 
        
        // Setup timer to expire after the minimum gate trigger pulse duration (100us)
        isPulseWidthDelay = true;
        __HAL_TIM_SET_AUTORELOAD( htim, 100 );
    } 
    // PHASE 2: Pulse Termination & Interval Calculation
    else {
        // Atomic search: Clear any pin that was opened by THIS sequencer session
        for ( int i = 0; i < TriacCount; i++ ) {
            if ( osEventFlagsGet( devices[ i ].flags ) & ( 1 << FlagTriacStatusPulseActive ) ) {
                HAL_GPIO_WritePin( devices[ i ].port, devices[ i ].pin, GPIO_PIN_SET );
                osEventFlagsClear( devices[ i ].flags, ( 1 << FlagTriacStatusGateOpen ) | 
                                                       ( 1 << FlagTriacStatusPulseActive ) | 
                                                       ( 1 << FlagTriacStatusPulsePending ) );
            }
        }

        isPulseWidthDelay = false;
        currentSeqIndex++;

        // If more TRIACs are in the queue, calculate the offset to the next delay
        if ( currentSeqIndex < sequenceCount ) {
            uint16_t nextDelay = devices[ activeSequence[ currentSeqIndex ] ].currentParams.phaseDelayUs;
            // 'spent' is the time already passed since ZCD (Previous Delay + 100us pulse)
            uint16_t spent = devices[ activeSequence[ currentSeqIndex - 1 ] ].currentParams.phaseDelayUs + 100;
            // Prevent negative loads if pulses overlap
            uint16_t load = ( nextDelay > spent ) ? ( nextDelay - spent ) : 1;
            __HAL_TIM_SET_AUTORELOAD( htim, load );
        } else {
            HAL_TIM_Base_Stop_IT( htim );
        }
    }
}

// Monitors ZCD health and asserts the global fault if the AC line is missing
void TriacProcess( void ) {
    // 50ms watchdog (5 cycles @ 50Hz); identifies total AC loss
    if ( ( osKernelGetTickCount() - lastZcdTick ) > 50 ) {
        for ( int i = 0; i < TriacCount; i++ ) {
            osEventFlagsSet( devices[ i ].flags, 1 << FlagTriacStatusZCDLost );
        }
        osEventFlagsSet( FaultFlagsHandle, 1 << FlagTriacFault );
    }
}