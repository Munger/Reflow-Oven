/// @file MCU.h
///
/// @brief STM32G0 MCU peripheral driver (internal ADC, RTC, power monitoring).
///
/// Reads the internal temperature sensor, VREFINT, and battery voltage from
/// the ADC DMA buffer. All getters return cached values and are safe to call
/// from any task context. Time writes are queued by MCUSetTime() and applied
/// by MCUProcess() to avoid blocking callers.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef MCU_H
#define MCU_H

#include <stdbool.h>

#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Logical identifiers for MCU instances managed by this driver.
typedef enum {
    MCU0 = 0,   ///< STM32G0 on-chip peripherals
    MCUCount
} MCUID;

/// @brief Status and diagnostic flag bit positions for the MCU module.
/// These map 1:1 to the bits in the private per-instance status event flag group.
typedef enum {
    FlagMCUStatusReady = 0,          ///< MCU peripherals initialised
    FlagMCUStatusLowBattery,          ///< Battery level below 15%
    FlagMCUStatusCriticalBattery,     ///< Battery level below 5%
    FlagMCUStatusOverTemp,            ///< Internal junction temperature above 80°C
    FlagMCUStatusVoltageUnstable,     ///< VCC outside the expected 3.0–3.6 V range
    FlagMCUStatusRtcInvalid,          ///< RTC set or read failed
    FlagMCUStatusAdcError,            ///< ADC DMA peripheral error
    FlagMCUStatusTimePending,         ///< RTC time write queued; not yet applied by MCUProcess().

    MCUFlagsCount
} MCUStatusBit;

_Static_assert( MCUFlagsCount <= 24, "MCUStatusFlags out of bounds" );

/// @brief Wall-clock time structure used for RTC get/set operations.
typedef struct {
    uint8_t  Hours;    ///< Hour of the day (0–23)
    uint8_t  Minutes;  ///< Minutes (0–59)
    uint8_t  Seconds;  ///< Seconds (0–59)
    uint8_t  Day;      ///< Day of the month (1–31)
    uint8_t  Month;    ///< Month (1–12)
    uint16_t Year;     ///< Full year (e.g. 2026)
} MCUTime, *MCUTimePtr;

/// @brief Opaque handle to an MCU instance.
typedef struct MCUInstance* MCURef;

/// @brief Allocate per-instance resources. Does not access hardware.
void        MCUInitModule( void );

/// @brief Return a handle to the specified MCU instance.
/// @param[in] id MCU instance identifier.
/// @return Handle to the instance, or NULL if @p id is out of range.
MCURef      MCUOpen( MCUID id );

/// @brief Return the supply voltage computed from the VREFINT ADC channel.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return VCC in millivolts (e.g. 3300 = 3.3 V); returns the last cached value on zero input.
/// @note Pure computation from the DMA buffer — safe to call from any task context.
Voltage     MCUGetVcc( MCURef mcu );

/// @brief Return the MCU junction temperature computed from the internal ADC channel.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Temperature in milli-degrees Celsius; returns the last cached value if VCC is zero.
/// @note Pure computation from the DMA buffer — safe to call from any task context.
Temperature MCUGetInternalTemp( MCURef mcu );

/// @brief Return the battery voltage from the VBAT ADC channel.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Battery voltage in millivolts; uses the last cached VCC for the scaling factor.
/// @note Pure computation from the DMA buffer — safe to call from any task context.
Voltage     MCUGetBatteryVoltage( MCURef mcu );

/// @brief Return the battery charge level as a permille value.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return 0 (empty, ≤3.0 V) to 1000 (full, ≥4.2 V).
/// @note Pure computation — no flag side-effects. Safe to call from any task context.
Permille    MCUGetBatteryLevel( MCURef mcu );

/// @brief Return the full status bitmask from the private instance status flags.
/// @param[in] mcu Handle returned by MCUOpen().
/// @return Bitmask of MCUStatusBit flags, or 0 if @p mcu is NULL.
uint32_t    MCUGetStatus( MCURef mcu );

/// @brief Read the current wall-clock time from the RTC.
/// @param[in]  mcu  Handle returned by MCUOpen().
/// @param[out] time Pointer to an MCUTime struct to populate.
/// @note Fast register read — no blocking. Safe to call from any task context.
void        MCUGetTime( MCURef mcu, MCUTimePtr time );

/// @brief Queue a wall-clock time update; applied by MCUProcess() on the next tick.
/// @param[in] mcu  Handle returned by MCUOpen().
/// @param[in] time Pointer to the new time to apply.
/// @note The critical section inside ensures atomicity of the pending-time write.
void        MCUSetTime( MCURef mcu, const MCUTimePtr time );

/// @brief Refresh cached ADC readings, apply pending RTC writes, and update status flags.
/// @warning All ADC and RTC hardware access occurs here. Do not call from ISR context.
void MCUProcess( void );

#endif // MCU_H
