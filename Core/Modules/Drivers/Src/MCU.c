/// @file MCU.c
///
/// @brief STM32G0 MCU peripheral driver — internal ADC, RTC, and power monitoring.
///
/// Reads the DMA-populated AdcDataBuffer for internal temperature, VREFINT,
/// and VBAT channels. All getter functions perform pure arithmetic from the DMA
/// buffer and are safe to call from any task context. MCUSetTime() queues a
/// pending RTC write that is applied by MCUProcess() to avoid blocking callers
/// at the point of the call. Status flags are set and cleared exclusively in
/// MCUProcess().
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

/// @brief DMA buffer populated by the ADC DMA transfer chain.
/// Index layout: 0–3 = thermistor channels, 4 = TSENSOR, 5 = VREFINT, 6 = VBAT.
extern AdcRaw AdcDataBuffer[ 7 ];

/// @brief ADC DMA buffer indices for internal channels (indices 4–6; 0–3 are thermistor channels).
enum { kIxTemp = 4, kIxVref = 5, kIxVbat = 6 };

/// @brief Internal per-instance state for the MCU driver.
typedef struct MCUInstance {
    MCUID            id;           ///< Instance identifier
    osEventFlagsId_t statusHandle; ///< Private status event flag group
    Temperature      lastTemp;     ///< Last computed junction temperature (milli-degrees C)
    Voltage          lastVcc;      ///< Last computed VCC supply voltage (millivolts)
    volatile MCUTime pendingTime;  ///< Pending RTC write, populated atomically by MCUSetTime()
} MCUInstance, *MCUInstancePtr;

/// @brief All MCU instances — indexed by MCUID.
static MCUInstance instances[ MCUCount ];

/// @brief Allocate per-instance resources. Does not access hardware.
void MCUInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    instances[ MCU0 ].id           = MCU0;
    instances[ MCU0 ].statusHandle = osEventFlagsNew( NULL );
}

/// @brief Return a handle to the specified MCU instance.
/// @param[in] id MCU instance identifier.
/// @return Handle to the instance, or NULL if @p id is out of range.
MCURef MCUOpen( MCUID id ) {
    if ( id >= MCUCount ) return NULL;
    MCUInstancePtr inst = &instances[ id ];
    osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusReady ) );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagMCUReady ) );
    return inst;
}

/// @brief Compute VCC from the VREFINT ADC reading using the factory calibration constant.
///
/// Uses the STM32 VREFINT_CAL_ADDR factory calibration value at 3.0 V to
/// back-calculate the actual supply voltage. Returns the last cached value if
/// the ADC reads zero (prevents division by zero on startup).
///
/// @param[in] mcu Handle returned by MCUOpen().
/// @return VCC in millivolts; returns last cached value if ADC is zero.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Voltage MCUGetVcc( MCURef mcu ) {
    if ( mcu == NULL ) return 0;
    uint16_t vref_raw = AdcDataBuffer[ kIxVref ];
    if ( vref_raw == 0 ) return mcu->lastVcc;
    mcu->lastVcc = ( 3000 * ( *VREFINT_CAL_ADDR ) ) / vref_raw;
    return mcu->lastVcc;
}

/// @brief Compute the internal junction temperature from the temperature-sensor ADC channel.
///
/// Applies the two-point factory calibration (TEMPSENSOR_CAL1 / CAL2) after
/// scaling the raw ADC count by the actual VCC ratio. Returns the last cached
/// value if VCC has not yet been measured.
///
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Junction temperature in milli-degrees Celsius.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Temperature MCUGetInternalTemp( MCURef mcu ) {
    if ( mcu == NULL ) return 0;
    uint16_t raw = AdcDataBuffer[ kIxTemp ];
    if ( mcu->lastVcc == 0 ) return mcu->lastTemp;
    int32_t raw_scaled = ( raw * mcu->lastVcc ) / 3000;
    int32_t temp = (int32_t)( 110000 - 30000 ) * ( raw_scaled - *TEMPSENSOR_CAL1_ADDR );
    temp          = temp / ( *TEMPSENSOR_CAL2_ADDR - *TEMPSENSOR_CAL1_ADDR );
    mcu->lastTemp = (Temperature)( temp + 30000 );
    return mcu->lastTemp;
}

/// @brief Compute the battery voltage from the VBAT ADC channel.
///
/// The STM32G0 connects VBAT/3 to the ADC; this function multiplies back by 3.
/// Relies on lastVcc being populated by MCUGetVcc() first.
///
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Battery voltage in millivolts; 0 if @p mcu is NULL.
/// @note Pure computation from DMA buffer — safe to call from any task context.
Voltage MCUGetBatteryVoltage( MCURef mcu ) {
    if ( mcu == NULL ) return 0;
    uint16_t raw = AdcDataBuffer[ kIxVbat ];
    return ( (uint32_t)raw * mcu->lastVcc * 3 ) / 4095;
}

/// @brief Compute the battery charge level as a permille fraction.
///
/// Maps the battery voltage linearly between 3.0 V (0 permille) and
/// 4.2 V (1000 permille) with hard clamps at both ends.
///
/// @param[in] mcu Handle returned by MCUOpen().
/// @return 0 (empty) to 1000 (full); 0 if @p mcu is NULL.
/// @note Safe to call from any task context.
Permille MCUGetBatteryLevel( MCURef mcu ) {
    Voltage vbat = MCUGetBatteryVoltage( mcu );
    if ( vbat >= 4200 ) return 1000;
    if ( vbat <= 3000 ) return 0;
    return (Permille)( ( ( vbat - 3000 ) * 1000 ) / ( 4200 - 3000 ) );
}

/// @brief Return the full status bitmask from the private instance status flags.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Bitmask of MCUStatusBit flags, or 0 if @p mcu is NULL.
uint32_t MCUGetStatus( MCURef mcu ) {
    return ( mcu != NULL ) ? osEventFlagsGet( mcu->statusHandle ) : 0;
}

/// @brief Read the current wall-clock time from the RTC hardware registers.
///
/// Calls HAL_RTC_GetTime and HAL_RTC_GetDate (which must be called in that
/// order per HAL requirements). Silently returns without modifying @p time on
/// HAL error.
///
/// @param[in]  mcu  Handle returned by MCUOpen().
/// @param[out] time Struct to populate with the current time and date.
/// @note Fast register read — no blocking. Safe to call from any task context.
void MCUGetTime( MCURef mcu, MCUTimePtr time ) {
    UNUSED( mcu );
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
/// @param[in] mcu  Handle returned by MCUOpen().
/// @param[in] time New time to apply on the next MCUProcess() tick.
void MCUSetTime( MCURef mcu, const MCUTimePtr time ) {
    if ( mcu == NULL || time == NULL ) return;
    taskENTER_CRITICAL();
    mcu->pendingTime = *time;
    taskEXIT_CRITICAL();
    osEventFlagsSet( mcu->statusHandle, BIT( FlagMCUStatusTimePending ) );
}

/// @brief Refresh cached ADC readings, apply any pending RTC write, and update status flags.
///
/// Evaluates VCC, temperature, and battery level thresholds and maps them to
/// status flag bits. Applies a pending time set if one was queued by MCUSetTime().
/// Propagates active faults to FaultFlagsHandle.
///
/// @warning All RTC hardware access occurs here. Do not call from ISR context.
void MCUProcess( void ) {
    MCUInstancePtr inst = &instances[ MCU0 ];
    if ( inst->statusHandle == NULL ) return;

    MCUGetVcc( inst );
    MCUGetInternalTemp( inst );
    Permille level = MCUGetBatteryLevel( inst );

    if ( osEventFlagsGet( inst->statusHandle ) & BIT( FlagMCUStatusTimePending ) ) {
        osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusTimePending ) );
        MCUTime t;
        taskENTER_CRITICAL();
        t = inst->pendingTime;
        taskEXIT_CRITICAL();
        RTC_TimeTypeDef sTime = { .Hours = t.Hours, .Minutes = t.Minutes, .Seconds = t.Seconds };
        RTC_DateTypeDef sDate = { .Date = t.Day, .Month = t.Month, .Year = (uint8_t)( t.Year - 2000 ) };
        if ( HAL_RTC_SetTime( &hrtc, &sTime, RTC_FORMAT_BIN ) == HAL_OK &&
             HAL_RTC_SetDate( &hrtc, &sDate, RTC_FORMAT_BIN ) == HAL_OK ) {
            osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusRtcInvalid ) );
        } else {
            osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusRtcInvalid ) );
        }
    }

    if ( inst->lastVcc < 3000 || inst->lastVcc > 3600 ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusVoltageUnstable ) );
    } else {
        osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusVoltageUnstable ) );
    }

    if ( inst->lastTemp > 80000 ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusOverTemp ) );
    } else {
        osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusOverTemp ) );
    }

    if ( level < 50 ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusCriticalBattery ) );
    } else {
        osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusCriticalBattery ) );
    }

    if ( level < 150 ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagMCUStatusLowBattery ) );
    } else {
        osEventFlagsClear( inst->statusHandle, BIT( FlagMCUStatusLowBattery ) );
    }

    uint32_t nonFaultMask = BIT( FlagMCUStatusReady ) | BIT( FlagMCUStatusTimePending );
    if ( osEventFlagsGet( inst->statusHandle ) & ~nonFaultMask ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagMCUFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagMCUFault ) );
    }
}
