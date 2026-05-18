/// @file MCU.h
///
/// @brief STM32G0 MCU peripheral driver (internal ADC, RTC, power monitoring, CRC).
///
/// Feature availability is controlled by Features.h. Each optional sub-system
/// (RTC, battery, internal temperature, VCC, hardware CRC) compiles away entirely
/// when its flag is set to 0.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef MCU_H
#define MCU_H

#include <stdbool.h>
#include <stddef.h>

#include "Features.h"
#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Logical identifiers for MCU instances managed by this driver.
typedef enum {
    MCU0 = 0,   ///< STM32G0 on-chip peripherals
    MCUCount
} MCUID;

/// @brief Status and diagnostic flag bit positions for the MCU module.
/// Flags for disabled features are defined but never set.
typedef enum {
    FlagMCUStatusReady = 0,          ///< MCU peripherals initialised
    FlagMCUStatusLowBattery,          ///< Battery level below 15%          — FEATURE_BATTERY_MONITOR
    FlagMCUStatusCriticalBattery,     ///< Battery level below 5%           — FEATURE_BATTERY_MONITOR
    FlagMCUStatusOverTemp,            ///< Junction temperature above 80°C  — FEATURE_MCU_TEMP_MONITOR
    FlagMCUStatusVoltageUnstable,     ///< VCC outside 3.0–3.6 V           — FEATURE_VCC_MONITOR
    FlagMCUStatusRtcInvalid,          ///< RTC set or read failed           — FEATURE_RTC
    FlagMCUStatusAdcError,            ///< ADC DMA peripheral error
    FlagMCUStatusCrcBusy,             ///< CRC engine acquired; computation in progress
    FlagMCUStatusTimePending,         ///< RTC time write queued            — FEATURE_RTC

    MCUFlagsCount
} MCUStatusBit;

_Static_assert( MCUFlagsCount <= 24, "MCUStatusFlags out of bounds" );

#if FEATURE_RTC
/// @brief Wall-clock time structure used for RTC get/set operations.
typedef struct {
    uint8_t  Hours;    ///< Hour of the day (0–23)
    uint8_t  Minutes;  ///< Minutes (0–59)
    uint8_t  Seconds;  ///< Seconds (0–59)
    uint8_t  Day;      ///< Day of the month (1–31)
    uint8_t  Month;    ///< Month (1–12)
    uint16_t Year;     ///< Full year (e.g. 2026)
} MCUTime, *MCUTimePtr;
#endif // FEATURE_RTC

/// @brief Opaque handle to an MCU instance.
typedef struct MCUInstance* MCURef;

/// @brief Allocate per-instance resources. Does not access hardware.
void        MCUInitModule( void );

/// @brief Return a handle to the specified MCU instance.
MCURef      MCUOpen( MCUID id );

/// @brief Return the full status bitmask from the private instance status flags.
uint32_t    MCUGetStatus( MCURef mcu );

/// @brief Refresh cached ADC readings, apply pending RTC writes, and update status flags.
/// @warning All ADC and RTC hardware access occurs here. Do not call from ISR context.
void        MCUProcess( void );

#if FEATURE_VCC_MONITOR
/// @brief Return the supply voltage computed from the VREFINT ADC channel.
Voltage     MCUGetVcc( MCURef mcu );
#endif // FEATURE_VCC_MONITOR

#if FEATURE_MCU_TEMP_MONITOR
/// @brief Return the MCU junction temperature computed from the internal ADC channel.
Temperature MCUGetInternalTemp( MCURef mcu );
#endif // FEATURE_MCU_TEMP_MONITOR

#if FEATURE_BATTERY_MONITOR
/// @brief Return the battery voltage from the VBAT ADC channel.
Voltage     MCUGetBatteryVoltage( MCURef mcu );

/// @brief Return the battery charge level as a permille value (0 = empty, 1000 = full).
Permille    MCUGetBatteryLevel( MCURef mcu );
#endif // FEATURE_BATTERY_MONITOR

#if FEATURE_RTC
/// @brief Read the current wall-clock time from the RTC.
void        MCUGetTime( MCURef mcu, MCUTimePtr time );

/// @brief Queue a wall-clock time update; applied by MCUProcess() on the next tick.
void        MCUSetTime( MCURef mcu, const MCUTimePtr time );
#endif // FEATURE_RTC

/// @brief Acquire the CRC engine and reset the accumulator. Blocks if already in use.
void        CRCInit( MCURef mcu );

/// @brief Feed a byte buffer into the running CRC-32 accumulator.
void        CRCUpdate( MCURef mcu, const uint8_t* data, size_t length );

/// @brief Return the final CRC-32 value and release the CRC engine.
uint32_t    CRCResult( MCURef mcu );

/// @brief Compute CRC-32 over a contiguous buffer. Acquires and releases the engine internally.
uint32_t    CRCCompute( MCURef mcu, const uint8_t* data, size_t length );

/// @brief Compute CRC-32 and compare with an expected value.
bool        CRCVerify( MCURef mcu, const uint8_t* data, size_t length, uint32_t expected );

#endif // MCU_H
