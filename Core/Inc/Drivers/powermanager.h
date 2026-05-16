/// @file PowerManager.h
///
/// @brief AC mains power management and E-Stop safety supervisor.
///
/// Controls the hot-side relay and auxiliary 24 V rail via GPIO. Tracks mains
/// presence, ZCD (zero-cross detection), and E-Stop state. PMProcess() reconciles
/// commanded and observed state each tick and raises faults as needed.
/// PMGetStatus() returns a cached bitmask safe from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef POWERMANAGER_H
#define POWERMANAGER_H

#include <stdbool.h>

#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Granular status and fault bit positions for the Power Manager.
/// These map 1:1 to the bits in the private pmStatus shadow word and are
/// mirrored into PowerManagerStatusFlagsHandle by PMProcess().
typedef enum {
    FlagPMStatusReady = 0,       ///< Power manager initialised and running

    // Hardware inputs and feedback
    FlagPMEStopTripped,           ///< E-Stop loop is open (hardware detected)
    FlagPMMainsPower,             ///< AC mains input detected (vs USB-only supply)
    FlagPMSwitchedACLive,         ///< ZCD confirms switched AC is present at the output

    // Command states
    FlagPMHotSideEnabled,         ///< Hot-side relay command is active (GPIO driven low)
    FlagPMAuxPowerEnabled,        ///< Aux 24 V rail is active

    // Derived logic / fault states
    FlagPMHotSideBlocked,         ///< E-Stop is preventing the hot side from turning on
    FlagPMHotSideRogue,           ///< AC live detected but hot side should be off (safety fault)
    FlagPMHotSideDead,            ///< Hot side commanded on but AC not detected (relay fault)

    PMFlagsCount
} PMStatusBit;

_Static_assert( PMFlagsCount <= 24, "PowerManagerStatusFlags out of bounds" );

/// @brief Initialise the Power Manager, sample GPIO state, and disable both output rails.
void             PMInitModule( void );

/// @brief Enable the hot-side relay if mains power is present and E-Stop is clear.
/// @return true if the relay was energised, false if blocked by E-Stop or no mains.
bool             PMEnableHotSide( void );

/// @brief Disable the hot-side relay unconditionally.
/// @return Always true.
bool             PMDisableHotSide( void );

/// @brief Enable the auxiliary 24 V power rail.
/// @return Always true.
bool             PMEnableAuxPower( void );

/// @brief Disable the auxiliary 24 V power rail.
/// @return Always true.
bool             PMDisableAuxPower( void );

/// @brief Return the current power manager status bitmask.
/// @return Bitmask of PMStatusBit flags; safe to call from any task context.
/// @note Returns the cached pmStatus shadow word — no hardware access.
uint32_t         PMGetStatus( void );

/// @brief Reconcile commanded and observed power state and update fault flags.
///
/// Checks ZCD watchdog, samples the E-Stop GPIO, computes rogue/dead/blocked
/// conditions, and propagates them to FaultFlagsHandle.
/// @warning Must only be called from task context. All GPIO reads occur here.
void             PMProcess( void );

/// @brief ZCD rising-edge ISR handler — records the tick and marks AC as live.
/// @param[in] GPIO_Pin The HAL pin mask (unused; only one ZCD pin exists).
/// @warning ISR context. Must not call any FreeRTOS blocking API.
void             PMHandleZCDInterrupt( uint16_t GPIO_Pin );

/// @brief E-Stop rising-edge ISR handler — immediately de-energises the hot-side relay.
/// @param[in] GPIO_Pin The HAL pin mask (unused; only one E-Stop pin exists).
/// @warning ISR context. Hardware isolation happens here before PMProcess() sees the event.
void             PMHandleEStopInterrupt( uint16_t GPIO_Pin );

#endif // POWERMANAGER_H
