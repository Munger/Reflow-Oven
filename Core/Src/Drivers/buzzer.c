#include <string.h>
#include "platform.h"
#include "main.h"
#include "tim.h"
#include "buzzer.h"

// Track the sequencer progress.
static volatile struct {
    const Melody* currentMelody;
    uint8_t       currentIndex;
    uint32_t      remainingToggles; 
    bool          isRest;
} sequencer;

// Sequences for system notifications.
static const struct {
    uint8_t    length;
    BuzzerTone tones[ 31 ];
} levelCompleteInternal = {
    .length = 31,
    .tones = {
        { NoteG7, 150 }, { NoteC8, 150 }, { NoteE8, 150 }, { NoteG7, 150 }, { NoteC8, 150 }, { NoteE8, 150 },
        { NoteGs7, 150 }, { NoteC8, 150 }, { NoteDs8, 150 }, { NoteGs7, 150 }, { NoteC8, 150 }, { NoteDs8, 150 },
        { NoteAs7, 150 }, { NoteD8, 150 }, { NoteE8, 150 }, { NoteAs7, 150 }, { NoteD8, 150 }, { NoteE8, 150 },
        { NoteC8, 200 }, { NoteRest, 50 }, { NoteC8, 200 }, { NoteRest, 50 }, { NoteC8, 200 }, { NoteRest, 50 },
        { NoteC8, 150 }, { NoteB7, 150 }, { NoteAs7, 150 }, { NoteA7, 150 },
        { NoteGs7, 400 }, { NoteAs7, 400 }, { NoteC8, 1000 }
    }
};

static const struct { uint8_t length; BuzzerTone tones[ 3 ]; } patternPowerOn = {
    .length = 3, .tones = { { NoteC7, 100 }, { NoteE7, 100 }, { NoteG7, 200 } }
};

static const struct { uint8_t length; BuzzerTone tones[ 2 ]; } patternSuccess = {
    .length = 2, .tones = { { NoteG7, 100 }, { NoteC8, 300 } }
};

static const struct { uint8_t length; BuzzerTone tones[ 3 ]; } patternError = {
    .length = 3, .tones = { { NoteC7, 200 }, { NoteRest, 50 }, { NoteC7, 400 } }
};

static const struct { uint8_t length; BuzzerTone tones[ 4 ]; } patternCritical = {
    .length = 4, .tones = { { NoteDs8, 150 }, { NoteRest, 50 }, { NoteDs8, 150 }, { NoteRest, 50 } }
};

static const Melody* patterns[] = {
    ( const Melody* )&patternPowerOn,
    ( const Melody* )&patternSuccess,
    ( const Melody* )&patternError,
    ( const Melody* )&patternCritical,
    ( const Melody* )&levelCompleteInternal
};

static void TimerHandler( TIM_HandleTypeDef *htim );

void BuzzerInitModule( void ) {
    memset( ( void* )&sequencer, 0, sizeof( sequencer ) );
    
    BuzzerStatusFlagsHandle = osEventFlagsNew( NULL );

    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    HAL_TIM_RegisterCallback( &htim7, HAL_TIM_PERIOD_ELAPSED_CB_ID, TimerHandler );
    
    // Internal ready flag only.
    osEventFlagsSet( BuzzerStatusFlagsHandle, BIT( FlagBuzzerStatusReady ) );
    // Signal system that hardware is initialized.
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagBuzzerReady ) );
}

static void BuzzerPrepareNote( void ) {
    const BuzzerTone* note = &sequencer.currentMelody->tones[ sequencer.currentIndex ];
    
    if ( note->tone == NoteRest ) {
        sequencer.isRest = true;
        sequencer.remainingToggles = note->durationMs / 10;
        __HAL_TIM_SET_AUTORELOAD( &htim7, 10000 ); 
    } else {
        sequencer.isRest = false;
        sequencer.remainingToggles = ( ( uint32_t )note->tone * note->durationMs ) / 500;
        uint32_t period = 500000 / ( uint32_t )note->tone;
        __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    }
    
    __HAL_TIM_SET_COUNTER( &htim7, 0 );
}

void BuzzerStart( BuzzerFrequency frequency ) {
    if ( frequency == NoteRest ) {
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
        return;
    }
    
    uint32_t period = 500000 / ( uint32_t )frequency;
    __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    
    osEventFlagsSet( BuzzerStatusFlagsHandle, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
}

void BuzzerStop( void ) {
    HAL_TIM_Base_Stop_IT( &htim7 );
    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    
    osEventFlagsClear( BuzzerStatusFlagsHandle, BIT( FlagBuzzerStatusActive ) );
}

void BuzzerPlay( const BuzzerPattern pattern ) {
    BuzzerPlayMelody( patterns[ ( uint8_t )pattern ] );
}

void BuzzerPlayMelody( const Melody* melody ) {
    if ( !melody || melody->length == 0 ) return;

    sequencer.currentMelody = melody;
    sequencer.currentIndex  = 0;

    BuzzerPrepareNote();
    
    osEventFlagsSet( BuzzerStatusFlagsHandle, BIT( FlagBuzzerStatusActive ) );
    HAL_TIM_Base_Start_IT( &htim7 );
}

void BuzzerProcess( void ) {
}   

static void TimerHandler( TIM_HandleTypeDef *htim ) {
    UNUSED( htim );

    uint32_t flags = osEventFlagsGet( BuzzerStatusFlagsHandle );
    if ( !( flags & BIT( FlagBuzzerStatusActive ) ) ) return;

    if ( !sequencer.isRest ) {
        HAL_GPIO_TogglePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin );
    } else {
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    }

    if ( sequencer.remainingToggles > 0 ) {
        sequencer.remainingToggles--;
    } else {
        sequencer.currentIndex++;
        
        if ( sequencer.currentIndex < sequencer.currentMelody->length ) {
            BuzzerPrepareNote();
        } else {
            HAL_TIM_Base_Stop_IT( &htim7 );
            HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
            
            osEventFlagsClear( BuzzerStatusFlagsHandle, BIT( FlagBuzzerStatusActive ) );
        }
    }
}