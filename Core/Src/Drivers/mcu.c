#include <string.h>

#include "adc.h"
#include "mcu.h"
#include "rtc.h"
#include "stm32g0xx_ll_adc.h"

// Buffer mapping based on adc.c configuration:
extern AdcRaw AdcDataBuffer[ 7 ];

#define IX_TEMP 4
#define IX_VREF 5
#define IX_VBAT 6

static Temperature lastTemp;
static Voltage     lastVcc;

// Internal helper to sync the global flag with the current state of the module flags.
static void SyncGlobalFault( void ) {
    if ( MCUStatusFlagsHandle == NULL ) return;

    // Mask out the Ready bit (0) to check if any other fault bits are set.
    uint32_t activeFaults = osEventFlagsGet( MCUStatusFlagsHandle ) & ~BIT( FlagMCUStatusReady );

    if ( activeFaults != 0 ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagMCUFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagMCUFault ) );
    }
}

void MCUInitModule( void ) {
    if ( MCUStatusFlagsHandle != NULL ) {
        osEventFlagsClear( MCUStatusFlagsHandle, 0xFFFFFF );
        osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusReady ) );
    }
}

Voltage MCUGetVcc( void ) {
    uint16_t vref_raw = AdcDataBuffer[ IX_VREF ];
    if ( vref_raw == 0 ) return 0;

    lastVcc = ( 3000 * ( *VREFINT_CAL_ADDR ) ) / vref_raw;

    if ( lastVcc < 3000 || lastVcc > 3600 ) {
        osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusVoltageUnstable ) );
    } else {
        osEventFlagsClear( MCUStatusFlagsHandle, BIT( FlagMCUStatusVoltageUnstable ) );
    }

    SyncGlobalFault();
    return lastVcc;
}

Temperature MCUGetInternalTemp( void ) {
    Voltage  vcc = MCUGetVcc();
    uint16_t raw = AdcDataBuffer[ IX_TEMP ];
    if ( vcc == 0 ) return 0;

    int32_t raw_scaled = ( raw * vcc ) / 3000;
    int32_t temp = (int32_t)( 110000 - 30000 ) * ( raw_scaled - *TEMPSENSOR_CAL1_ADDR );
    temp = temp / ( *TEMPSENSOR_CAL2_ADDR - *TEMPSENSOR_CAL1_ADDR );
    lastTemp = (Temperature)( temp + 30000 );

    if ( lastTemp > 80000 ) {
        osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusOverTemp ) );
    } else {
        osEventFlagsClear( MCUStatusFlagsHandle, BIT( FlagMCUStatusOverTemp ) );
    }

    SyncGlobalFault();
    return lastTemp;
}

Voltage MCUGetBatteryVoltage( void ) {
    Voltage  vcc = MCUGetVcc();
    uint16_t raw = AdcDataBuffer[ IX_VBAT ];
    return ( raw * vcc * 3 ) / 4095;
}

Permille MCUGetBatteryLevel( void ) {
    Voltage vbat = MCUGetBatteryVoltage();
    Permille level;

    if ( vbat >= 4200 ) level = 1000;
    else if ( vbat <= 3000 ) level = 0;
    else level = (Permille)( ( ( vbat - 3000 ) * 1000 ) / ( 4200 - 3000 ) );

    if ( level < 50 ) osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusCriticalBattery ) );
    else osEventFlagsClear( MCUStatusFlagsHandle, BIT( FlagMCUStatusCriticalBattery ) );
    
    if ( level < 150 ) osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusLowBattery ) );
    else osEventFlagsClear( MCUStatusFlagsHandle, BIT( FlagMCUStatusLowBattery ) );

    SyncGlobalFault();
    return level;
}

uint32_t MCUGetStatus( void ) {
    return ( MCUStatusFlagsHandle != NULL ) ? osEventFlagsGet( MCUStatusFlagsHandle ) : 0;
}

void MCUGetTime( MCUTimePtr time ) {
    if ( !time ) return;
    RTC_TimeTypeDef sTime = { 0 };
    RTC_DateTypeDef sDate = { 0 };

    if ( HAL_RTC_GetTime( &hrtc, &sTime, RTC_FORMAT_BIN ) == HAL_OK ) {
        HAL_RTC_GetDate( &hrtc, &sDate, RTC_FORMAT_BIN );
        time->Hours   = sTime.Hours;
        time->Minutes = sTime.Minutes;
        time->Seconds = sTime.Seconds;
        time->Day     = sDate.Date;
        time->Month   = sDate.Month;
        time->Year    = 2000 + sDate.Year;
    } else {
        osEventFlagsSet( MCUStatusFlagsHandle, BIT( FlagMCUStatusRtcInvalid ) );
        SyncGlobalFault();
    }
}

void MCUSetTime( const MCUTimePtr time ) {
    if ( !time ) return;
    RTC_TimeTypeDef sTime = { .Hours = time->Hours, .Minutes = time->Minutes, .Seconds = time->Seconds };
    RTC_DateTypeDef sDate = { .Date = time->Day, .Month = time->Month, .Year = (uint8_t)( time->Year - 2000 ) };

    if ( HAL_RTC_SetTime( &hrtc, &sTime, RTC_FORMAT_BIN ) == HAL_OK &&
         HAL_RTC_SetDate( &hrtc, &sDate, RTC_FORMAT_BIN ) == HAL_OK ) {
        osEventFlagsClear( MCUStatusFlagsHandle, BIT( FlagMCUStatusRtcInvalid ) );
        SyncGlobalFault();
    }
}

void MCUProcess( void ) {
    MCUGetInternalTemp();
    MCUGetBatteryLevel();
}