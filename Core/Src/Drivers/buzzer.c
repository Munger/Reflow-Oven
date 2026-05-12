#include <string.h>

#include "main.h"
#include "tim.h"
#include "buzzer.h"

// Volatile state for ISR safe access
static volatile struct {
    const Melody* currentMelody;
    uint8_t       currentIndex;
    uint32_t      remainingToggles; 
    bool          isPlaying;
    bool          isRest;
} state;

// Internal storage for the level complete jingle
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

void BuzzerInitModule( void ) {
    memset( ( void* )&state, 0, sizeof( state ) );
    // Gate High = P-MOS OFF
    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
}

static void BuzzerPrepareNote( void ) {
    const BuzzerTone* note = &state.currentMelody->tones[ state.currentIndex ];
    
    if ( note->tone == NoteRest ) {
        state.isRest = true;
        state.remainingToggles = note->durationMs / 10;
        __HAL_TIM_SET_AUTORELOAD( &htim7, 10000 ); 
    } else {
        state.isRest = false;
        // Total toggles = ( Frequency * Duration ) / 500
        state.remainingToggles = ( ( uint32_t )note->tone * note->durationMs ) / 500;
        // Half-period ticks @ 1MHz
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
    // Simple direct start logic if needed outside of PlayMelody
    uint32_t period = 500000 / ( uint32_t )frequency;
    __HAL_TIM_SET_AUTORELOAD( &htim7, period );
    HAL_TIM_Base_Start_IT( &htim7 );
}

void BuzzerStop( void ) {
    HAL_TIM_Base_Stop_IT( &htim7 );
    HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    state.isPlaying = false;
}

void BuzzerPlay( const BuzzerPattern pattern ) {
    BuzzerPlayMelody( patterns[ ( uint8_t )pattern ] );
}

void BuzzerPlayMelody( const Melody* melody ) {
    if ( !melody || melody->length == 0 ) return;

    state.currentMelody = melody;
    state.currentIndex  = 0;
    state.isPlaying     = true;

    BuzzerPrepareNote();
    HAL_TIM_Base_Start_IT( &htim7 );
}

void BuzzerProcess( void ) {
    if ( !state.isPlaying ) return;

    if ( !state.isRest ) {
        HAL_GPIO_TogglePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin );
    } else {
        HAL_GPIO_WritePin( BUZZER_EN_N_GPIO_Port, BUZZER_EN_N_Pin, GPIO_PIN_SET );
    }

    if ( state.remainingToggles > 0 ) {
        state.remainingToggles--;
    } else {
        state.currentIndex++;
        
        if ( state.currentIndex < state.currentMelody->length ) {
            BuzzerPrepareNote();
        } else {
            BuzzerStop();
        }
    }
}