/// @file PowerManager.c
///
/// @brief AC mains power management and E-Stop safety supervisor implementation.
///
/// Uses a 32-bit shadow word (pmStatus) as the source of truth for all power
/// state rather than re-reading GPIOs on every getter call. PMProcess() reconciles
/// the shadow against observed hardware state and computes derived fault conditions.
/// The ZCD and E-Stop ISR handlers write only to pmStatus and the last-ZCD tick —
/// they never call FreeRTOS blocking APIs or drive FaultFlagsHandle directly.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "PowerManager.h"

/// @brief Private event flag group for power manager status.
static osEventFlagsId_t powerManagerStatus;

/// @brief Shadow word holding the current logical power state.
///
/// Written atomically by PMProcess() and the ISR handlers.
/// PMGetStatus() returns this value directly without any HAL reads.
static volatile uint32_t pmStatus = 0;

/// @brief HAL tick value from the most recent ZCD interrupt.
///
/// Used by PMProcess() to detect AC loss: if the gap since the last ZCD exceeds
/// 50 ms, FlagPMSwitchedACLive is cleared (5 missed cycles at 50 Hz).
static uint32_t lastZCDTick = 0;

/// @brief Initialise the Power Manager — sample GPIO state, disable output rails, set ready flag.
///
/// Reads the MAINS_PWR_N and ESTOP pins to populate the initial shadow state,
/// then drives both output rails to their safe (off) state. Signals PMReady in
/// DeviceStatusFlagsHandle.
void PMInitModule( void ) {
    powerManagerStatus = osEventFlagsNew( NULL );

    if ( HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin ) == GPIO_PIN_RESET ) {
        pmStatus |= BIT( FlagPMMainsPower );
    }

    if ( HAL_GPIO_ReadPin( ESTOP_GPIO_Port, ESTOP_Pin ) == GPIO_PIN_SET ) {
        pmStatus |= BIT( FlagPMEStopTripped );
    }

    PMDisableHotSide();
    PMDisableAuxPower();

    pmStatus |= BIT( FlagPMStatusReady );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagPowerManagerReady ) );
}

/// @brief Reconcile commanded and observed power state and update fault flags.
///
/// Checks the ZCD watchdog (50 ms timeout), re-reads the E-Stop GPIO, computes
/// the derived fault conditions (rogue, dead, blocked), and mirrors the full
/// pmStatus bitmask into powerManagerStatus. Propagates E-Stop and power manager
/// faults to FaultFlagsHandle.
///
/// @warning All GPIO reads and flag updates occur here. Do not call from ISR context.
void PMProcess( void ) {
    // ZCD watchdog: clear FlagPMSwitchedACLive if no rising edge in the last 50 ms
    if ( ( HAL_GetTick() - lastZCDTick ) > 50 ) {
        pmStatus &= ~BIT( FlagPMSwitchedACLive );
    }

    // Re-sample E-Stop and force relay off if it tripped
    if ( HAL_GPIO_ReadPin( ESTOP_GPIO_Port, ESTOP_Pin ) == GPIO_PIN_SET ) {
        pmStatus |= BIT( FlagPMEStopTripped );
        if ( pmStatus & BIT( FlagPMHotSideEnabled ) ) {
            PMDisableHotSide();
        }
    } else {
        pmStatus &= ~BIT( FlagPMEStopTripped );
    }

    // Clear derived fault flags before re-evaluation
    pmStatus &= ~( BIT( FlagPMHotSideRogue ) | BIT( FlagPMHotSideDead ) | BIT( FlagPMHotSideBlocked ) );

    bool cmd_on  = ( pmStatus & BIT( FlagPMHotSideEnabled ) );
    bool ac_live = ( pmStatus & BIT( FlagPMSwitchedACLive ) );
    bool tripped = ( pmStatus & BIT( FlagPMEStopTripped ) );

    if ( !cmd_on && ac_live ) {
        // AC detected at the output when the relay should be open — possible relay weld
        pmStatus |= BIT( FlagPMHotSideRogue );
    } else if ( cmd_on && !ac_live && !tripped ) {
        // Relay commanded on but ZCD sees no AC — possible relay failure
        pmStatus |= BIT( FlagPMHotSideDead );
    } else if ( cmd_on && tripped ) {
        // Relay commanded on but E-Stop is active — command blocked by safety interlock
        pmStatus |= BIT( FlagPMHotSideBlocked );
    }

    if ( tripped ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagESTOP ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagESTOP ) );
    }

    if ( pmStatus & ( BIT( FlagPMHotSideRogue ) | BIT( FlagPMHotSideDead ) ) ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagPowerManagerFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagPowerManagerFault ) );
    }

    // Mirror complete shadow to the event flag group for external observers
    osEventFlagsSet( powerManagerStatus, pmStatus );
}

/// @brief Enable the hot-side relay if mains power is present and E-Stop is clear.
///
/// Writes the relay GPIO and sets FlagPMHotSideEnabled atomically inside a
/// critical section. Returns false without GPIO writes if preconditions fail.
///
/// @return true if the relay was energised, false if blocked.
bool PMEnableHotSide( void ) {
    if ( ( pmStatus & BIT( FlagPMMainsPower ) ) && !( pmStatus & BIT( FlagPMEStopTripped ) ) ) {
        taskENTER_CRITICAL();
        pmStatus |= BIT( FlagPMHotSideEnabled );
        HAL_GPIO_WritePin( HOT_SIDE_PWR_EN_N_GPIO_Port, HOT_SIDE_PWR_EN_N_Pin, GPIO_PIN_RESET );
        taskEXIT_CRITICAL();
        return true;
    }
    return false;
}

/// @brief Disable the hot-side relay unconditionally.
/// @return Always true.
bool PMDisableHotSide( void ) {
    taskENTER_CRITICAL();
    pmStatus &= ~BIT( FlagPMHotSideEnabled );
    HAL_GPIO_WritePin( HOT_SIDE_PWR_EN_N_GPIO_Port, HOT_SIDE_PWR_EN_N_Pin, GPIO_PIN_SET );
    taskEXIT_CRITICAL();
    return true;
}

/// @brief Enable the auxiliary 24 V power rail.
/// @return Always true.
bool PMEnableAuxPower( void ) {
    taskENTER_CRITICAL();
    pmStatus |= BIT( FlagPMAuxPowerEnabled );
    HAL_GPIO_WritePin( AUX_24V_EN_N_GPIO_Port, AUX_24V_EN_N_Pin, GPIO_PIN_RESET );
    taskEXIT_CRITICAL();
    return true;
}

/// @brief Disable the auxiliary 24 V power rail.
/// @return Always true.
bool PMDisableAuxPower( void ) {
    taskENTER_CRITICAL();
    pmStatus &= ~BIT( FlagPMAuxPowerEnabled );
    HAL_GPIO_WritePin( AUX_24V_EN_N_GPIO_Port, AUX_24V_EN_N_Pin, GPIO_PIN_SET );
    taskEXIT_CRITICAL();
    return true;
}

/// @brief Return the current power manager status bitmask.
/// @return Cached pmStatus shadow word; safe to call from any task context.
uint32_t PMGetStatus( void ) {
    return pmStatus;
}

/// @brief ZCD rising-edge ISR handler — records the timestamp and marks AC as live.
///
/// Records the HAL tick and sets FlagPMSwitchedACLive in the shadow word.
/// PMProcess() checks for timeout on every tick to detect AC loss.
/// @warning ISR context. Must not call any FreeRTOS blocking API.
void PMHandleZCDInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    lastZCDTick = HAL_GetTick();
    pmStatus |= BIT( FlagPMSwitchedACLive );
}

/// @brief E-Stop rising-edge ISR handler — immediately de-energises the hot-side relay.
///
/// Hardware has already isolated the hot side before this ISR fires (hardware
/// interlock). This handler mirrors the state in software so that PMProcess()
/// sees the correct flags on the next tick and raises FlagESTOP.
/// @warning ISR context. Must not call any FreeRTOS blocking API.
void PMHandleEStopInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    // Hardware has already isolated hot side before this ISR fires.
    // Mirror the state in software; PMProcess() will raise FlagESTOP on next tick.
    pmStatus &= ~BIT( FlagPMHotSideEnabled );
    HAL_GPIO_WritePin( HOT_SIDE_PWR_EN_N_GPIO_Port, HOT_SIDE_PWR_EN_N_Pin, GPIO_PIN_SET );
    pmStatus |= BIT( FlagPMEStopTripped );
}
