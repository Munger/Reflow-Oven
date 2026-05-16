/// @file MCU.c
///
/// @brief STM32G0 MCU peripheral driver — internal ADC, RTC, and power monitoring.
///
/// Reads the DMA-populated AdcDataBuffer for internal temperature, VREFINT,
/// and VBAT channels. All getter functions perform pure arithmetic from the DMA
/// buffer and are safe to call from any task context. MCUSetTime() queues a
/// pending RTC write that is applied by MCUProcess() to avoid blocking callers
/// at the point of the call. Status flags are set and cleared exclusively in
/// MCUProcess() and the private SyncGlobalFault() helper.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "adc.h"
#include "MCU.h"
#include "rtc.h"
#include "stm32g0xx_ll_adc.h"

/// @brief DMA buffer populated by the ADC DMA transfer chain.
/// Index layout: 0–3 = thermistor channels, 4 = TSENSOR, 5 = VREFINT, 6 = VBAT.
extern AdcRaw AdcDataBuffer[ 7 ];

/// @name ADC DMA buffer indices for internal channels
/// @{
#define IX_TEMP 4  ///< Internal temperature sensor
#define IX_VREF 5  ///< VREFINT calibration channel
#define IX_VBAT 6  ///< Battery voltage (VBAT/3)
/// @}

/// @brief Private event flag group for MCU module status.
/// @note Replaces the former public MCUStatusFlagsHandle extern.
static osEventFlagsId_t mcuStatus;

/// @brief Last computed MCU junction temperature (milli-degrees C).
/// Updated by MCUGetInternalTemp(); used as a fallback when ADC reads zero.
static Temperature lastTemp;

/// @brief Last computed VCC supply voltage (millivolts).
/// Updated by MCUGetVcc(); used as the scaling reference for temperature and battery.
static Voltage lastVcc;

/// @brief Pending RTC time to write, populated atomically by MCUSetTime().
static volatile MCUTime pendingTime    = { 0 };

/// @brief True when an MCUSetTime() call is pending application by MCUProcess().
static volatile bool    pendingTimeSet = false;

/// @brief Propagate any non-ready MCU fault to the global FaultFlagsHandle.
///
/// If any status bit other than FlagMCUStatusReady is set, FlagMCUFault is raised.
/// When all faults are clear, FlagMCUFault is cleared. Called at the end of MCUProcess().
static void SyncGlobalFault( void ) {
    if ( mcuStatus == NULL ) return;
    uint32_t activeFaults = osEventFlagsGet( mcuStatus ) & ~BIT( FlagMCUStatusReady );
    if ( activeFaults != 0 ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagMCUFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagMCUFault ) );
    }
}

/// @brief Initialise internal MCU peripherals and signal system readiness.
///
/// Creates the private mcuStatus event flag group, clears all flags, and
/// sets FlagMCUStatusReady. Signals FlagMCUReady in DeviceStatusFlagsHandle.
void MCUInitModule( void ) {
    mcuStatus = osEventFlagsNew( NULL );

    if ( mcuStatus != NULL ) {
        osEventFlagsClear( mcuStatus, 0xFFFFFF );
        osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusReady ) );
    }
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagMCUReady ) );
}

/// @brief Compute VCC from the VREFINT ADC reading using the factory calibration constant.
///
/// Uses the STM32 VREFINT_CAL_ADDR factory calibration value at 3.0 V to
/// back-calculate the actual supply voltage. Returns the last cached value if
/// the ADC reads zero (prevents division by zero on startup).
///
/// @return VCC in millivolts; returns last cached value if ADC is zero.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Voltage MCUGetVcc( void ) {
    uint16_t vref_raw = AdcDataBuffer[ IX_VREF ];
    if ( vref_raw == 0 ) return lastVcc;
    lastVcc = ( 3000 * ( *VREFINT_CAL_ADDR ) ) / vref_raw;
    return lastVcc;
}

/// @brief Compute the internal junction temperature from the temperature-sensor ADC channel.
///
/// Applies the two-point factory calibration (TEMPSENSOR_CAL1 / CAL2) after
/// scaling the raw ADC count by the actual VCC ratio. Returns the last cached
/// value if VCC has not yet been measured.
///
/// @return Junction temperature in milli-degrees Celsius.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Temperature MCUGetInternalTemp( void ) {
    uint16_t raw = AdcDataBuffer[ IX_TEMP ];
    if ( lastVcc == 0 ) return lastTemp;
    int32_t raw_scaled = ( raw * lastVcc ) / 3000;
    int32_t temp = (int32_t)( 110000 - 30000 ) * ( raw_scaled - *TEMPSENSOR_CAL1_ADDR );
    temp     = temp / ( *TEMPSENSOR_CAL2_ADDR - *TEMPSENSOR_CAL1_ADDR );
    lastTemp = (Temperature)( temp + 30000 );
    return lastTemp;
}

/// @brief Compute the battery voltage from the VBAT ADC channel.
///
/// The STM32G0 connects VBAT/3 to the ADC; this function multiplies back by 3.
/// Relies on lastVcc being populated by MCUGetVcc() first.
///
/// @return Battery voltage in millivolts.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Voltage MCUGetBatteryVoltage( void ) {
    uint16_t raw = AdcDataBuffer[ IX_VBAT ];
    return ( (uint32_t)raw * lastVcc * 3 ) / 4095;
}

/// @brief Compute the battery charge level as a permille fraction.
///
/// Maps the battery voltage linearly between 3.0 V (0 permille) and
/// 4.2 V (1000 permille) with hard clamps at both ends.
///
/// @return 0 (empty) to 1000 (full). No flag side-effects.
/// @note Safe to call from any task context.
Permille MCUGetBatteryLevel( void ) {
    Voltage vbat = MCUGetBatteryVoltage();
    if ( vbat >= 4200 ) return 1000;
    if ( vbat <= 3000 ) return 0;
    return (Permille)( ( ( vbat - 3000 ) * 1000 ) / ( 4200 - 3000 ) );
}

/// @brief Return the full status bitmask from the private mcuStatus flags.
/// @return Bitmask of MCUStatusBit flags, or 0 if the flag group has not been initialised.
uint32_t MCUGetStatus( void ) {
    return ( mcuStatus != NULL ) ? osEventFlagsGet( mcuStatus ) : 0;
}

/// @brief Read the current wall-clock time from the RTC hardware registers.
///
/// Calls HAL_RTC_GetTime and HAL_RTC_GetDate (which must be called in that
/// order per HAL requirements). Silently returns without modifying @p time on
/// HAL error.
///
/// @param[out] time Struct to populate with the current time and date.
/// @note Fast register read — no blocking. Safe to call from any task context.
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
    }
}

/// @brief Queue a wall-clock time update for deferred application by MCUProcess().
///
/// Copies @p time into pendingTime inside a critical section to ensure the
/// multi-field write is atomic with respect to the task scheduler.
///
/// @param[in] time New time to apply on the next MCUProcess() tick.
void MCUSetTime( const MCUTimePtr time ) {
    if ( !time ) return;
    taskENTER_CRITICAL();
    pendingTime    = *time;
    pendingTimeSet = true;
    taskEXIT_CRITICAL();
}

/// @brief Refresh cached ADC readings, apply any pending RTC write, and update status flags.
///
/// Evaluates VCC, temperature, and battery level thresholds and maps them to
/// mcuStatus flag bits. Applies a pending time set if one was queued by MCUSetTime().
/// Calls SyncGlobalFault() at the end to propagate any active faults.
///
/// @warning All RTC hardware access occurs here. Do not call from ISR context.
void MCUProcess( void ) {
    MCUGetVcc();
    MCUGetInternalTemp();
    Permille level = MCUGetBatteryLevel();

    if ( pendingTimeSet ) {
        pendingTimeSet = false;
        MCUTime t;
        taskENTER_CRITICAL();
        t = pendingTime;
        taskEXIT_CRITICAL();
        RTC_TimeTypeDef sTime = { .Hours = t.Hours, .Minutes = t.Minutes, .Seconds = t.Seconds };
        RTC_DateTypeDef sDate = { .Date = t.Day, .Month = t.Month, .Year = (uint8_t)( t.Year - 2000 ) };
        if ( HAL_RTC_SetTime( &hrtc, &sTime, RTC_FORMAT_BIN ) == HAL_OK &&
             HAL_RTC_SetDate( &hrtc, &sDate, RTC_FORMAT_BIN ) == HAL_OK ) {
            osEventFlagsClear( mcuStatus, BIT( FlagMCUStatusRtcInvalid ) );
        } else {
            osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusRtcInvalid ) );
        }
    }

    if ( lastVcc < 3000 || lastVcc > 3600 ) {
        osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusVoltageUnstable ) );
    } else {
        osEventFlagsClear( mcuStatus, BIT( FlagMCUStatusVoltageUnstable ) );
    }

    if ( lastTemp > 80000 ) {
        osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusOverTemp ) );
    } else {
        osEventFlagsClear( mcuStatus, BIT( FlagMCUStatusOverTemp ) );
    }

    if ( level < 50 ) {
        osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusCriticalBattery ) );
    } else {
        osEventFlagsClear( mcuStatus, BIT( FlagMCUStatusCriticalBattery ) );
    }

    if ( level < 150 ) {
        osEventFlagsSet( mcuStatus, BIT( FlagMCUStatusLowBattery ) );
    } else {
        osEventFlagsClear( mcuStatus, BIT( FlagMCUStatusLowBattery ) );
    }

    SyncGlobalFault();
}
