/// @file SystemStatusFlags.h
///
/// @brief Global event flag group definitions for cross-task system state.
///
/// Declares the three public `osEventFlagsId_t` handles that coordinate system
/// state across all tasks and drivers. These handles are created by
/// `app_freertos.c` (CubeMX-generated) at startup and must not be privatised —
/// they are the authoritative, shared source of truth for:
///   - `SystemStatusFlagsHandle`  — kernel-level milestones (initialised, ISR enabled)
///   - `DeviceStatusFlagsHandle`  — per-driver ready bits, forming `DEVICE_ALL_READY`
///   - `FaultFlagsHandle`         — active fault bits, forming `FAULT_ANY`
///
/// Reflow execution phase flags live in `Reflow.h` / `ReflowFlagsHandle`,
/// which is created by CubeMX alongside the other handles.
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
#include "Features.h"

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
    FlagSystemInitialised = 0,    ///< All modules are initialised; tasks may proceed.
    FlagInterruptsEnabled,        ///< GPIO edge interrupts have been unmasked.
    FlagSupervisorServiceRequest, ///< ManagerTask: a supervisor action is requested.
    FlagSystemAborted,            ///< Software interrupt has fired; system is aborting.
    FlagAPIDebug,                 ///< API diagnostic detail toggle — include driver status flags in hardware GET responses.

    SystemFlagsCount              ///< Number of flags — must stay <= 24.
} SystemFlagBit;

/// @brief Per-device ready flags, stored in DeviceStatusFlagsHandle.
///
/// Each driver sets its own bit in `XxxInitModule()` once its hardware is
/// configured and its event flag group is allocated. ManagerTask waits for
/// `DEVICE_ALL_READY` (all bits set) before enabling interrupts.
/// Flags for disabled features are defined but never set.
typedef enum {
    FlagMCUReady = 0,            ///< MCU peripheral init complete.
    FlagUSBPDReady,              ///< USB-PD controller init complete.          — FEATURE_USB_PD

    FlagThermocouple1Ready,      ///< MAX31856 channel 1 ready.                 — FEATURE_THERMOCOUPLE_1
    FlagThermocouple2Ready,      ///< MAX31856 channel 2 ready.                 — FEATURE_THERMOCOUPLE_2

    FlagThermistorCJT1Ready,     ///< Cold-junction thermistor 1 (MCP3221) ready. — FEATURE_THERMISTOR_CJT_1
    FlagThermistorCJT2Ready,     ///< Cold-junction thermistor 2 (MCP3221) ready. — FEATURE_THERMISTOR_CJT_2
    FlagThermistorOvenReady,     ///< Oven cavity thermistor (MCP3221) ready.   — FEATURE_THERMISTOR_OVEN
    FlagThermistorHeatsinkReady, ///< Heatsink thermistor (EMC2101 I2C) ready.  — FEATURE_THERMISTOR_HEATSINK

    FlagPowerManagerReady,       ///< Power manager init complete.
    FlagBuzzerReady,             ///< Buzzer driver init complete.               — FEATURE_BUZZER

    FlagOvenFanReady,            ///< Oven fan (AC) driver ready.               — FEATURE_OVEN_FAN
    FlagBoardFanReady,           ///< Board cooling fan (DC, EMC2101) ready.    — FEATURE_BOARD_FAN
    FlagTriacReady,              ///< TRIAC phase-angle driver ready.
    FlagFlashReady,              ///< External NOR flash init and verified.      — FEATURE_FLASH
    FlagHeaterTopReady,          ///< Top heating element TRIAC channel ready.  — FEATURE_HEATER_TOP
    FlagHeaterRearReady,         ///< Rear heating element TRIAC channel ready. — FEATURE_HEATER_REAR
    FlagHeaterBottomReady,       ///< Bottom heating element TRIAC channel ready. — FEATURE_HEATER_BOTTOM
    FlagOvenLightReady,          ///< Oven interior light TRIAC channel ready.  — FEATURE_OVEN_LIGHT
    FlagOvenControllerReady,     ///< Oven controller initialised and all device refs valid.
    FlagUSBCDCReady,             ///< USB CDC driver initialised.
    FlagRotaryEncoderReady,      ///< Rotary encoder (AS5600) initialised and I2C ref valid. — FEATURE_ROTARY_ENCODER

    DeviceFlagsCount             ///< Number of flags — must stay <= 24.
} DeviceFlagsBit;

/// @brief Active fault flags, stored in FaultFlagsHandle.
///
/// Any bit set here indicates an active fault condition. ManagerTask blocks on
/// `FAULT_ANY` and is expected to enforce a safe state when a fault occurs.
/// Flags for disabled features are defined but never set.
typedef enum {
    FlagESTOP = 0,               ///< Emergency stop was triggered.
    FlagMCUFault,                ///< MCU peripheral or watchdog fault.
    FlagPowerManagerFault,       ///< Power supply out of range.
    FlagUSBPDFault,              ///< USB-PD negotiation or hardware fault.        — FEATURE_USB_PD
    FlagThermocouple1Fault,      ///< Thermocouple 1 open-circuit or CRC error.   — FEATURE_THERMOCOUPLE_1
    FlagThermocouple2Fault,      ///< Thermocouple 2 open-circuit or CRC error.   — FEATURE_THERMOCOUPLE_2
    FlagThermistorCJT1Fault,     ///< Cold-junction thermistor 1 read failure.    — FEATURE_THERMISTOR_CJT_1
    FlagThermistorCJT2Fault,     ///< Cold-junction thermistor 2 read failure.    — FEATURE_THERMISTOR_CJT_2
    FlagThermistorOvenFault,     ///< Oven cavity thermistor read failure.         — FEATURE_THERMISTOR_OVEN
    FlagThermistorHeatsinkFault, ///< Heatsink thermistor read failure.            — FEATURE_THERMISTOR_HEATSINK
    FlagOvenFanFault,            ///< Oven fan stall or speed error.               — FEATURE_OVEN_FAN
    FlagBoardFanFault,           ///< Board fan stall or speed error.              — FEATURE_BOARD_FAN
    FlagBuzzerFault,             ///< Buzzer hardware fault.                       — FEATURE_BUZZER
    FlagReflowFault,             ///< Reflow profile logic error.
    FlagI2CFault,                ///< I2C bus error or timeout.
    FlagSPIFault,                ///< SPI bus error or timeout.
    FlagTriacFault,              ///< TRIAC gate or ZCD fault.
    FlagFlashFault,              ///< NOR flash SPI error or JEDEC ID mismatch.    — FEATURE_FLASH
    FlagHeaterTopFault,          ///< Top heating element TRIAC configuration error.    — FEATURE_HEATER_TOP
    FlagHeaterRearFault,         ///< Rear heating element TRIAC configuration error.   — FEATURE_HEATER_REAR
    FlagHeaterBottomFault,       ///< Bottom heating element TRIAC configuration error. — FEATURE_HEATER_BOTTOM
    FlagOvenLightFault,          ///< Oven interior light TRIAC configuration error.    — FEATURE_OVEN_LIGHT
    FlagOvenControllerFault,     ///< Oven controller regulation fault or required sensor failure.
    FlagUSBCFault,                ///< USB CDC hardware or transmit error.

    FaultFlagsCount              ///< Number of flags — must stay <= 24.
} FaultFlagsBit;

// ============================================================================
// DEVICE_ALL_READY — feature-gated helper macros.
// Each optional entry resolves to its ready bit or 0UL when disabled.
// ============================================================================

#if FEATURE_USB_PD
#define DEVBIT_USBPD            BIT( FlagUSBPDReady )
#else
#define DEVBIT_USBPD            0UL
#endif // FEATURE_USB_PD

#if FEATURE_THERMOCOUPLE_1
#define DEVBIT_TC1              BIT( FlagThermocouple1Ready )
#else
#define DEVBIT_TC1              0UL
#endif // FEATURE_THERMOCOUPLE_1

#if FEATURE_THERMOCOUPLE_2
#define DEVBIT_TC2              BIT( FlagThermocouple2Ready )
#else
#define DEVBIT_TC2              0UL
#endif // FEATURE_THERMOCOUPLE_2

#if FEATURE_THERMISTOR_CJT_1
#define DEVBIT_CJT1             BIT( FlagThermistorCJT1Ready )
#else
#define DEVBIT_CJT1             0UL
#endif // FEATURE_THERMISTOR_CJT_1

#if FEATURE_THERMISTOR_CJT_2
#define DEVBIT_CJT2             BIT( FlagThermistorCJT2Ready )
#else
#define DEVBIT_CJT2             0UL
#endif // FEATURE_THERMISTOR_CJT_2

#if FEATURE_THERMISTOR_OVEN
#define DEVBIT_OVEN_NTC         BIT( FlagThermistorOvenReady )
#else
#define DEVBIT_OVEN_NTC         0UL
#endif // FEATURE_THERMISTOR_OVEN

#if FEATURE_THERMISTOR_HEATSINK
#define DEVBIT_HEATSINK         BIT( FlagThermistorHeatsinkReady )
#else
#define DEVBIT_HEATSINK         0UL
#endif // FEATURE_THERMISTOR_HEATSINK

#if FEATURE_BUZZER
#define DEVBIT_BUZZER           BIT( FlagBuzzerReady )
#else
#define DEVBIT_BUZZER           0UL
#endif // FEATURE_BUZZER

#if FEATURE_OVEN_FAN
#define DEVBIT_OVEN_FAN         BIT( FlagOvenFanReady )
#else
#define DEVBIT_OVEN_FAN         0UL
#endif // FEATURE_OVEN_FAN

#if FEATURE_BOARD_FAN
#define DEVBIT_BOARD_FAN        BIT( FlagBoardFanReady )
#else
#define DEVBIT_BOARD_FAN        0UL
#endif // FEATURE_BOARD_FAN

#define DEVBIT_TRIAC            BIT( FlagTriacReady )

#if FEATURE_FLASH
#define DEVBIT_FLASH            BIT( FlagFlashReady )
#else
#define DEVBIT_FLASH            0UL
#endif // FEATURE_FLASH

#if FEATURE_HEATER_TOP
#define DEVBIT_HEATER_TOP       BIT( FlagHeaterTopReady )
#else
#define DEVBIT_HEATER_TOP       0UL
#endif // FEATURE_HEATER_TOP

#if FEATURE_HEATER_REAR
#define DEVBIT_HEATER_REAR      BIT( FlagHeaterRearReady )
#else
#define DEVBIT_HEATER_REAR      0UL
#endif // FEATURE_HEATER_REAR

#if FEATURE_HEATER_BOTTOM
#define DEVBIT_HEATER_BOTTOM    BIT( FlagHeaterBottomReady )
#else
#define DEVBIT_HEATER_BOTTOM    0UL
#endif // FEATURE_HEATER_BOTTOM

#if FEATURE_OVEN_LIGHT
#define DEVBIT_OVEN_LIGHT       BIT( FlagOvenLightReady )
#else
#define DEVBIT_OVEN_LIGHT       0UL
#endif // FEATURE_OVEN_LIGHT

#if FEATURE_ROTARY_ENCODER
#define DEVBIT_ROTARY           BIT( FlagRotaryEncoderReady )
#else
#define DEVBIT_ROTARY           0UL
#endif // FEATURE_ROTARY_ENCODER

/// @brief Bitmask of all device-ready bits that must be set before the system initialises.
///
/// ManagerTask calls `osEventFlagsWait(DeviceStatusFlagsHandle, DEVICE_ALL_READY, ...)`
/// to block until every fitted driver has signalled readiness.
#define DEVICE_ALL_READY ( \
    BIT( FlagMCUReady )        | \
    BIT( FlagPowerManagerReady ) | \
    BIT( FlagOvenControllerReady ) | \
    BIT( FlagUSBCDCReady )     | \
    DEVBIT_USBPD               | \
    DEVBIT_TC1                 | \
    DEVBIT_TC2                 | \
    DEVBIT_CJT1                | \
    DEVBIT_CJT2                | \
    DEVBIT_OVEN_NTC            | \
    DEVBIT_HEATSINK            | \
    DEVBIT_BUZZER              | \
    DEVBIT_OVEN_FAN            | \
    DEVBIT_BOARD_FAN           | \
    DEVBIT_TRIAC               | \
    DEVBIT_FLASH               | \
    DEVBIT_HEATER_TOP          | \
    DEVBIT_HEATER_REAR         | \
    DEVBIT_HEATER_BOTTOM       | \
    DEVBIT_OVEN_LIGHT          | \
    DEVBIT_ROTARY                \
)

// ============================================================================
// FAULT_ANY — feature-gated helper macros.
// Each optional entry resolves to its fault bit or 0UL when disabled.
// ============================================================================

#if FEATURE_USB_PD
#define FAULTBIT_USBPD          BIT( FlagUSBPDFault )
#else
#define FAULTBIT_USBPD          0UL
#endif // FEATURE_USB_PD

#if FEATURE_THERMOCOUPLE_1
#define FAULTBIT_TC1            BIT( FlagThermocouple1Fault )
#else
#define FAULTBIT_TC1            0UL
#endif // FEATURE_THERMOCOUPLE_1

#if FEATURE_THERMOCOUPLE_2
#define FAULTBIT_TC2            BIT( FlagThermocouple2Fault )
#else
#define FAULTBIT_TC2            0UL
#endif // FEATURE_THERMOCOUPLE_2

#if FEATURE_THERMISTOR_CJT_1
#define FAULTBIT_CJT1           BIT( FlagThermistorCJT1Fault )
#else
#define FAULTBIT_CJT1           0UL
#endif // FEATURE_THERMISTOR_CJT_1

#if FEATURE_THERMISTOR_CJT_2
#define FAULTBIT_CJT2           BIT( FlagThermistorCJT2Fault )
#else
#define FAULTBIT_CJT2           0UL
#endif // FEATURE_THERMISTOR_CJT_2

#if FEATURE_THERMISTOR_OVEN
#define FAULTBIT_OVEN_NTC       BIT( FlagThermistorOvenFault )
#else
#define FAULTBIT_OVEN_NTC       0UL
#endif // FEATURE_THERMISTOR_OVEN

#if FEATURE_THERMISTOR_HEATSINK
#define FAULTBIT_HEATSINK       BIT( FlagThermistorHeatsinkFault )
#else
#define FAULTBIT_HEATSINK       0UL
#endif // FEATURE_THERMISTOR_HEATSINK

#if FEATURE_OVEN_FAN
#define FAULTBIT_OVEN_FAN       BIT( FlagOvenFanFault )
#else
#define FAULTBIT_OVEN_FAN       0UL
#endif // FEATURE_OVEN_FAN

#if FEATURE_BOARD_FAN
#define FAULTBIT_BOARD_FAN      BIT( FlagBoardFanFault )
#else
#define FAULTBIT_BOARD_FAN      0UL
#endif // FEATURE_BOARD_FAN

#if FEATURE_BUZZER
#define FAULTBIT_BUZZER         BIT( FlagBuzzerFault )
#else
#define FAULTBIT_BUZZER         0UL
#endif // FEATURE_BUZZER

#define FAULTBIT_REFLOW         BIT( FlagReflowFault )

#define FAULTBIT_TRIAC          BIT( FlagTriacFault )

#if FEATURE_FLASH
#define FAULTBIT_FLASH          BIT( FlagFlashFault )
#else
#define FAULTBIT_FLASH          0UL
#endif // FEATURE_FLASH

#if FEATURE_HEATER_TOP
#define FAULTBIT_HEATER_TOP     BIT( FlagHeaterTopFault )
#else
#define FAULTBIT_HEATER_TOP     0UL
#endif // FEATURE_HEATER_TOP

#if FEATURE_HEATER_REAR
#define FAULTBIT_HEATER_REAR    BIT( FlagHeaterRearFault )
#else
#define FAULTBIT_HEATER_REAR    0UL
#endif // FEATURE_HEATER_REAR

#if FEATURE_HEATER_BOTTOM
#define FAULTBIT_HEATER_BOTTOM  BIT( FlagHeaterBottomFault )
#else
#define FAULTBIT_HEATER_BOTTOM  0UL
#endif // FEATURE_HEATER_BOTTOM

#if FEATURE_OVEN_LIGHT
#define FAULTBIT_OVEN_LIGHT     BIT( FlagOvenLightFault )
#else
#define FAULTBIT_OVEN_LIGHT     0UL
#endif // FEATURE_OVEN_LIGHT

/// @brief Bitmask of all fault flags. ManagerTask uses this to wait on any fault.
#define FAULT_ANY ( \
    BIT( FlagESTOP )           | \
    BIT( FlagMCUFault )        | \
    BIT( FlagPowerManagerFault ) | \
    BIT( FlagI2CFault )        | \
    BIT( FlagSPIFault )        | \
    BIT( FlagOvenControllerFault ) | \
    BIT( FlagUSBCFault )       | \
    FAULTBIT_USBPD             | \
    FAULTBIT_TC1               | \
    FAULTBIT_TC2               | \
    FAULTBIT_CJT1              | \
    FAULTBIT_CJT2              | \
    FAULTBIT_OVEN_NTC          | \
    FAULTBIT_HEATSINK          | \
    FAULTBIT_OVEN_FAN          | \
    FAULTBIT_BOARD_FAN         | \
    FAULTBIT_BUZZER            | \
    FAULTBIT_REFLOW            | \
    FAULTBIT_TRIAC             | \
    FAULTBIT_FLASH             | \
    FAULTBIT_HEATER_TOP        | \
    FAULTBIT_HEATER_REAR       | \
    FAULTBIT_HEATER_BOTTOM     | \
    FAULTBIT_OVEN_LIGHT          \
)

/// @brief System milestone event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t SystemStatusFlagsHandle;

/// @brief Per-device readiness event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t DeviceStatusFlagsHandle;

/// @brief Active fault event flag group. Created by app_freertos.c at startup.
extern osEventFlagsId_t FaultFlagsHandle;

_Static_assert( SystemFlagsCount <= 24, "SystemStatusFlags out of bounds" );
_Static_assert( DeviceFlagsCount <= 24, "DeviceStatusFlags out of bounds" );
_Static_assert( FaultFlagsCount  <= 24, "FaultStatusFlags out of bounds" );

#endif // SYSTEM_STATUS_FLAGS_H
