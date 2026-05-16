/// @file SystemStatusFlags.h
///
/// @brief Global event flag group definitions for cross-task system state.
///
/// Declares the four public `osEventFlagsId_t` handles that coordinate system
/// state across all tasks and drivers. These handles are created by
/// `app_freertos.c` (CubeMX-generated) at startup and must not be privatised —
/// they are the authoritative, shared source of truth for:
///   - `SystemStatusFlagsHandle`  — kernel-level milestones (initialised, ISR enabled)
///   - `DeviceStatusFlagsHandle`  — per-driver ready bits, forming `DEVICE_ALL_READY`
///   - `FaultFlagsHandle`         — active fault bits, forming `FAULT_ANY`
///   - `ReflowStatusFlagsHandle`  — reflow profile execution phase
///
/// Also provides the `BIT(n)` and `OS_USER_FLAGS_MASK` utility macros and the
/// composite bitmask constants `DEVICE_ALL_READY` and `FAULT_ANY`.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef SYSTEM_STATUS_FLAGS_H
#define SYSTEM_STATUS_FLAGS_H

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "event_groups.h"

/// @brief Shift a flag index to its bitmask position.
/// @param n  Zero-based flag index.
#define BIT( n ) ( 1UL << ( n ) )

/// @brief CMSIS-OS2 restricts user event flags to bits 0–23.
///        Use this mask to clear or test only the user-accessible bits.
#define OS_USER_FLAGS_MASK    0x00FFFFFFUL

/// @brief System milestone flags, stored in SystemStatusFlagsHandle.
///
/// These bits track kernel-level lifecycle events that gate task startup.
typedef enum {
    FlagSystemInitialised = 0,    ///< All drivers are ready; tasks may proceed.
    FlagInterruptsEnabled,        ///< GPIO edge interrupts have been unmasked.
    FlagSupervisorServiceRequest, ///< ManagerTask: a supervisor action is requested.

    SystemFlagsCount              ///< Number of flags — must stay <= 24.
} SystemFlagBit;

/// @brief Per-device ready flags, stored in DeviceStatusFlagsHandle.
///
/// Each driver sets its own bit in `XxxInitModule()` once its hardware is
/// configured and its event flag group is allocated. ManagerTask waits for
/// `DEVICE_ALL_READY` (all bits set) before enabling interrupts.
typedef enum {
    FlagMCUReady = 0,            ///< MCU peripheral init complete.
    FlagUSBPDReady,              ///< USB-PD controller init complete.
    FlagUSBPDContractReady,      ///< USB-PD power contract negotiated.

    FlagThermocouple1Ready,      ///< MAX31856 channel 1 ready.
    FlagThermocouple2Ready,      ///< MAX31856 channel 2 ready.

    FlagThermistorCJT1Ready,     ///< Cold-junction thermistor 1 (MCP3221) ready.
    FlagThermistorCJT2Ready,     ///< Cold-junction thermistor 2 (MCP3221) ready.
    FlagThermistorOvenReady,     ///< Oven cavity thermistor (MCP3221) ready.
    FlagThermistorHeatsinkReady, ///< Heatsink thermistor (EMC2101 I2C) ready.

    FlagPowerManagerReady,       ///< Power manager init complete.
    FlagBuzzerReady,             ///< Buzzer driver init complete.

    FlagOvenFanReady,            ///< Oven fan (AC) driver ready.
    FlagOvenFanSpinning,         ///< Oven fan rotor is currently rotating.
    FlagBoardFanReady,           ///< Board cooling fan (DC, EMC2101) ready.
    FlagTriacReady,              ///< TRIAC phase-angle driver ready.
    FlagFlashReady,              ///< External NOR flash init and verified.

    DeviceFlagsCount             ///< Number of flags — must stay <= 24.
} DeviceFlagsBit;

/// @brief Reflow profile execution phase flags, stored in ReflowStatusFlagsHandle.
typedef enum {
    FlagReflowInProgress = 0,   ///< A reflow cycle is actively running.
    FlagReflowHeating,          ///< Oven is in the preheat/ramp-up phase.
    FlagReflowSoaking,          ///< Oven is in the thermal soak phase.
    FlagReflowCooling,          ///< Oven is in the forced-cool phase.
    FlagReflowDone,             ///< Reflow cycle completed successfully.

    ReflowFlagsCount            ///< Number of flags — must stay <= 24.
} ReflowStatusBit;

/// @brief Active fault flags, stored in FaultFlagsHandle.
///
/// Any bit set here indicates an active fault condition. ManagerTask blocks on
/// `FAULT_ANY` and is expected to enforce a safe state when a fault occurs.
typedef enum {
    FlagESTOP = 0,               ///< Emergency stop was triggered.
    FlagMCUFault,                ///< MCU peripheral or watchdog fault.
    FlagPowerManagerFault,       ///< Power supply out of range.
    FlagUSBPDFault,              ///< USB-PD negotiation or hardware fault.
    FlagThermocouple1Fault,      ///< Thermocouple 1 open-circuit or CRC error.
    FlagThermocouple2Fault,      ///< Thermocouple 2 open-circuit or CRC error.
    FlagThermistorCJT1Fault,     ///< Cold-junction thermistor 1 read failure.
    FlagThermistorCJT2Fault,     ///< Cold-junction thermistor 2 read failure.
    FlagThermistorOvenFault,     ///< Oven cavity thermistor read failure.
    FlagThermistorHeatsinkFault, ///< Heatsink thermistor read failure.
    FlagOvenFanFault,            ///< Oven fan stall or speed error.
    FlagBoardFanFault,           ///< Board fan stall or speed error.
    FlagBuzzerFault,             ///< Buzzer hardware fault.
    FlagReflowFault,             ///< Reflow profile logic error.
    FlagI2CFault,                ///< I2C bus error or timeout.
    FlagSPIFault,                ///< SPI bus error or timeout.
    FlagTriacFault,              ///< TRIAC gate or ZCD fault.
    FlagFlashFault,              ///< NOR flash SPI error or JEDEC ID mismatch.

    FaultFlagsCount              ///< Number of flags — must stay <= 24.
} FaultFlagsBit;

/// @brief Bitmask of all device-ready bits that must be set before the system initialises.
///
/// ManagerTask calls `osEventFlagsWait(DeviceStatusFlagsHandle, DEVICE_ALL_READY, ...)`
/// to block until every driver has signalled readiness. Note that
/// `FlagUSBPDContractReady` and `FlagOvenFanSpinning` are intentionally excluded
/// as they are set asynchronously after startup.
#define DEVICE_ALL_READY ( \
    BIT( FlagMCUReady )                | \
    BIT( FlagUSBPDReady )              | \
    BIT( FlagThermocouple1Ready )      | \
    BIT( FlagThermocouple2Ready )      | \
    BIT( FlagThermistorCJT1Ready )     | \
    BIT( FlagThermistorCJT2Ready )     | \
    BIT( FlagThermistorOvenReady )     | \
    BIT( FlagThermistorHeatsinkReady ) | \
    BIT( FlagPowerManagerReady )       | \
    BIT( FlagBuzzerReady )             | \
    BIT( FlagOvenFanReady )            | \
    BIT( FlagBoardFanReady )           | \
    BIT( FlagTriacReady )              | \
    BIT( FlagFlashReady )               \
)

/// @brief Bitmask of all fault flags. ManagerTask uses this to wait on any fault.
#define FAULT_ANY ( \
    BIT( FlagESTOP )                   | \
    BIT( FlagMCUFault )                | \
    BIT( FlagPowerManagerFault )       | \
    BIT( FlagUSBPDFault )              | \
    BIT( FlagThermocouple1Fault )      | \
    BIT( FlagThermocouple2Fault )      | \
    BIT( FlagThermistorCJT1Fault )     | \
    BIT( FlagThermistorCJT2Fault )     | \
    BIT( FlagThermistorOvenFault )     | \
    BIT( FlagThermistorHeatsinkFault ) | \
    BIT( FlagOvenFanFault )            | \
    BIT( FlagBoardFanFault )           | \
    BIT( FlagBuzzerFault )             | \
    BIT( FlagReflowFault )             | \
    BIT( FlagI2CFault )                | \
    BIT( FlagSPIFault )                | \
    BIT( FlagTriacFault )              | \
    BIT( FlagFlashFault )               \
)

/// @brief System milestone event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t SystemStatusFlagsHandle;

/// @brief Per-device readiness event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t DeviceStatusFlagsHandle;

/// @brief Active fault event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t FaultFlagsHandle;

/// @brief Reflow phase event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t ReflowStatusFlagsHandle;

_Static_assert( SystemFlagsCount <= 24, "SystemStatusFlags out of bounds" );
_Static_assert( DeviceFlagsCount <= 24, "DeviceStatusFlags out of bounds" );
_Static_assert( ReflowFlagsCount <= 24, "ReflowStatusFlags out of bounds" );
_Static_assert( FaultFlagsCount  <= 24, "FaultStatusFlags out of bounds" );

#endif // SYSTEM_STATUS_FLAGS_H
