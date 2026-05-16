/// @file Triac.h
///
/// @brief Phase-angle and burst-fire TRIAC driver for AC load control.
///
/// Manages five TRIAC channels. ZCDHandler() synchronises the sequencer to the
/// AC zero-cross; a TIM16 ISR generates the phase-angle gate pulses. All flags
/// are private per-instance (stored in the TriacDevice struct). TriacGetStatus()
/// returns cached flags safe from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef TRIAC_H
#define TRIAC_H

#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Status and diagnostic flag bit positions for a TRIAC channel.
/// These map 1:1 to the bits in each instance's private event flag group.
typedef enum {
    FlagTriacStatusReady = 0,        ///< Driver initialised and GPIO mapped
    FlagTriacStatusActive,            ///< Power is requested (manual ON or burst window ON)
    FlagTriacStatusGateOpen,          ///< Physical GPIO is LOW (gate is conducting)
    FlagTriacStatusZCDLost,           ///< AC line sync lost (ZCD watchdog timeout)
    FlagTriacStatusConfigError,       ///< Invalid parameters detected in TriacRun()
    FlagTriacStatusPhaseAngle,        ///< Sequenced phase-angle control mode is active
    FlagTriacStatusPulsePending,      ///< Sequencer has queued this channel for a timed pulse
    FlagTriacStatusPulseActive,       ///< This channel is currently inside its 100 µs pulse window

    TriacFlagsCount
} TriacStatusBit;

_Static_assert( TriacFlagsCount <= 24, "TriacStatusFlags out of bounds" );

/// @brief Logical identifiers for the five AC load channels.
typedef enum {
    TriacHeaterTop    = 0,  ///< Top heating element
    TriacHeaterRear,         ///< Rear heating element
    TriacHeaterBottom,       ///< Bottom heating element
    TriacOvenFan,            ///< AC oven circulation fan
    TriacLight,              ///< Oven interior light

    TriacCount
} TriacID;

/// @brief TRIAC drive parameters for a single channel.
typedef struct {
    uint16_t phaseDelayUs;  ///< Delay from ZCD to gate fire (0 = full power, up to 10000 µs)
    uint8_t  burstOn;       ///< Number of half-cycles 'On' within the burst window
    uint8_t  burstWindow;   ///< Total window size in half-cycles (burstOn + burstOff)
} TriacDriveParams, *TriacDriveParamsPtr;

/// @brief Opaque handle to a TRIAC channel instance.
typedef struct TriacDevice* TriacRef;

/// @brief Initialise all TRIAC channels, create per-instance event flags, and register TIM16 callback.
///
/// Sets all gate GPIOs to the inactive (HIGH, active-low) state and sets
/// FlagTriacStatusReady for each channel. Signals FlagTriacReady in DeviceStatusFlagsHandle.
void     TriacInitModule( void );

/// @brief Return a handle to the specified TRIAC channel.
/// @param[in] id Channel identifier from TriacID.
/// @return Handle to the instance, or NULL if @p id is out of range.
TriacRef TriacOpen( TriacID id );

/// @brief Drive a TRIAC at full power, bypassing phase-angle sequencing.
/// @param[in] triac Handle returned by TriacOpen().
/// @note Clears FlagTriacStatusPhaseAngle and asserts the gate GPIO directly.
void     TriacOn( TriacRef triac );

/// @brief De-energise a TRIAC and remove it from sequenced control.
/// @param[in] triac Handle returned by TriacOpen().
/// @note Clears FlagTriacStatusPhaseAngle, Active, and GateOpen flags.
void     TriacOff( TriacRef triac );

/// @brief Configure a TRIAC for phase-angle / burst-fire control and validate the parameters.
///
/// Validates that phaseDelayUs ≤ 10000 µs and burstOn ≤ burstWindow.
/// On validation failure, sets FlagTriacStatusConfigError and raises FlagTriacFault.
/// On success, loads the parameters and sets FlagTriacStatusPhaseAngle.
///
/// @param[in] triac  Handle returned by TriacOpen().
/// @param[in] params Drive parameters to apply.
/// @warning phaseDelayUs must be ≤ 10000 (one 50 Hz half-cycle). The TRIAC will
///          not fire for the current half-cycle; the change takes effect at the next ZCD.
void     TriacRun( TriacRef triac, TriacDriveParams params );

/// @brief Return the full status bitmask for a TRIAC channel.
/// @param[in] triac Handle returned by TriacOpen().
/// @return Bitmask of TriacStatusBit flags; 0 if @p triac is NULL.
/// @note Returns cached event flags — safe to call from any task context.
uint32_t TriacGetStatus( TriacRef triac );

/// @brief Task-loop tick: check the ZCD watchdog and assert ZCDLost / TriacFault on AC loss.
/// @warning Must be called from task context.
void     TriacProcess( void );

/// @brief Zero-cross rising-edge ISR handler — drives the burst sequencer and primes TIM16.
///
/// Resets all sequencer transient flags, updates burst counters, identifies
/// channels that need a timed gate pulse, sorts them by phase delay, and
/// arms TIM16 to fire at the first channel's phase offset.
///
/// @param[in] GPIO_Pin HAL pin mask (unused; only one ZCD pin exists).
/// @warning ISR context. Sets event flags and writes GPIO only. No FreeRTOS blocking APIs.
void     ZCDHandler( uint16_t GPIO_Pin );

#endif // TRIAC_H
