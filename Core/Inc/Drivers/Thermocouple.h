/// @file Thermocouple.h
///
/// @brief MAX31856 dual thermocouple driver.
///
/// Manages two thermocouple instances over SPI. All hardware I/O is performed
/// in TCProcess(); getters return cached values safe to call from any task
/// context. The DRDY and FAULT interrupt handlers only set volatile flags or
/// write to instance event groups — they never block or perform SPI transactions.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef THERMOCOUPLE_H
#define THERMOCOUPLE_H

#include "Types.h"
#include "SystemStatusFlags.h"
#include "SPIManager.h"

/// @brief Identifiers for the two thermocouple channels.
typedef enum {
    Thermocouple1 = 0,  ///< Primary thermocouple (inside oven, top position)
    Thermocouple2        ///< Secondary thermocouple (inside oven, bottom position)
} ThermocoupleID;

/// @brief Status and fault flag bit positions for a MAX31856 channel.
///
/// Each thermocouple instance has its own private event flag group.
/// These bit positions are identical for both instances.
typedef enum {
    FlagTCStatusOK = 0,          ///< No fault conditions active
    FlagTCStatusDataReady,        ///< DRDY pin asserted; new conversion result available
    FlagTCStatusOpenCircuit,      ///< MAX31856 OC fault bit — thermocouple wire is broken
    FlagTCStatusShortToGND,       ///< MAX31856 SCG fault bit — thermocouple shorted to ground
    FlagTCStatusShortToVCC,       ///< MAX31856 SCV fault bit — thermocouple shorted to supply
    FlagTCStatusCJTRangeLow,      ///< Cold junction temperature below the configured lower limit
    FlagTCStatusCJTRangeHigh,     ///< Cold junction temperature above the configured upper limit
    FlagTCStatusRangeLow,         ///< Thermocouple temperature below the configured lower limit
    FlagTCStatusRangeHigh,        ///< Thermocouple temperature above the configured upper limit
    FlagTCStatusCJTMismatch,      ///< Injected CJT differs significantly from internal measurement
    FlagTCStatusHardwareFault,    ///< SPI communication failure or internal IC error
    FlagTCStatusSamplePending,    ///< CJT injection and conversion cycle queued; not yet applied.
    FlagTCStatusFaultPending,     ///< FAULT ISR fired; status register not yet read by TCProcess().

    TCFlagsCount
} ThermocoupleStatusBit;

_Static_assert( TCFlagsCount <= 24, "ThermocoupleStatusFlags out of bounds" );

/// @brief Opaque handle to a thermocouple instance.
typedef struct Thermocouple* ThermocoupleRef;

/// @brief Initialise both thermocouple instances, configure the MAX31856 registers over SPI.
///
/// Creates per-instance private event flag groups via osEventFlagsNew().
/// Configures Type-K thermocouple mode, auto-conversion, and open-circuit detection.
void               TCInitModule( void );

/// @brief Open a handle to a specific thermocouple instance and configure the MAX31856.
///
/// On first call for a given ID: stores the SPI bus reference, writes CR0/CR1 to
/// the MAX31856 hardware, resolves the CJT thermistor reference, and signals
/// DeviceStatusFlagsHandle. Subsequent calls with the same ID return the existing
/// instance without re-configuring hardware.
///
/// @param[in] thermocoupleID Channel identifier (Thermocouple1 or Thermocouple2).
/// @param[in] spi            SPI bus handle returned by SPIOpen().
/// @return Handle to the instance.
ThermocoupleRef    TCOpen( ThermocoupleID thermocoupleID, SPIRef spi );

/// @brief Signal that a new conversion should begin on the next TCProcess() tick.
/// @param[in] tc Handle returned by TCOpen().
/// @note Sets the pendingSample flag only. Actual SPI write occurs in TCProcess().
void               TCRequestSample( ThermocoupleRef tc );

/// @brief Return true if a new sample has been decoded since the last call or flag clear.
/// @param[in] tc Handle returned by TCOpen().
/// @return true if FlagTCStatusDataReady is set in the instance status flags.
/// @note Safe to call from any task context without blocking.
bool               TCIsReady( ThermocoupleRef tc );

/// @brief Return the most recently decoded thermocouple temperature.
/// @param[in] tc Handle returned by TCOpen().
/// @return Temperature in milli-degrees Celsius; 0 if @p tc is NULL.
/// @note Returns a cached value; safe to call from any task context.
Temperature        TCGetTemperature( ThermocoupleRef tc );

/// @brief Return the cold-junction temperature for this thermocouple instance.
///
/// Delegates to TMGetTemperature() on the associated external NTC thermistor.
///
/// @param[in] tc Handle returned by TCOpen().
/// @return CJT in milli-degrees Celsius; 0 if either @p tc or its CJT reference is NULL.
/// @note Returns a cached value; safe to call from any task context.
Temperature        TCGetCJT( ThermocoupleRef tc );

/// @brief Return the full status bitmask for this thermocouple instance.
/// @param[in] tc Handle returned by TCOpen().
/// @return Bitmask of ThermocoupleStatusBit flags; returns FlagTCStatusHardwareFault if NULL.
/// @note Safe to call from any task context without blocking.
uint32_t           TCGetStatus( ThermocoupleRef tc );

/// @brief Service both thermocouple instances: inject CJT, decode temperature, read fault register.
///
/// Iterates both instances each call. For each instance:
///   - If a sample was requested, writes the CJT to the MAX31856 via SPI.
///   - If DRDY is set (by ISR), reads and decodes the temperature registers.
///   - If a fault is pending (set by ISR), reads the status register.
///   - Propagates hardware faults to FaultFlagsHandle.
///
/// @warning All SPI hardware access occurs here. Do not call from ISR context.
void               TCProcess( void );

/// @brief ISR handler for the DRDY pin — sets FlagTCStatusDataReady in the matching instance.
/// @param[in] GPIO_Pin HAL pin mask; compared against each instance's drdyPin.
/// @warning ISR context. Sets event flags only — no SPI access, no FreeRTOS blocking API.
void               TCHandleDRDYInterrupt( uint16_t GPIO_Pin );

/// @brief ISR handler for the FAULT pin — sets FlagTCStatusFaultPending in the matching instance.
/// @param[in] GPIO_Pin HAL pin mask; compared against each instance's faultPin.
/// @warning ISR context. osEventFlagsSet() only — SPI fault register read is deferred to TCProcess().
void               TCHandleFaultInterrupt( uint16_t GPIO_Pin );

#endif // THERMOCOUPLE_H
