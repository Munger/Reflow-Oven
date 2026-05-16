#include "powermanager.h"
#include "main.h"

// Hardware bit manipulation is atomic on Cortex-M.
static volatile uint32_t pmStatus = 0;
static uint32_t          lastZCDTick = 0;

void PMInitModule( void ) {
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

void PMProcess( void ) {
    // Check ZCD health.
    if ( ( HAL_GetTick() - lastZCDTick ) > 50 ) {
        pmStatus &= ~BIT( FlagPMSwitchedACLive );
    }

    // Update E-Stop state from pin.
    if ( HAL_GPIO_ReadPin( ESTOP_GPIO_Port, ESTOP_Pin ) == GPIO_PIN_SET ) {
        pmStatus |= BIT( FlagPMEStopTripped );
        if ( pmStatus & BIT( FlagPMHotSideEnabled ) ) {
            PMDisableHotSide();
        }
    } else {
        pmStatus &= ~BIT( FlagPMEStopTripped );
    }

    // Clear and recalculate logic-driven bits.
    pmStatus &= ~( BIT( FlagPMHotSideRogue ) | BIT( FlagPMHotSideDead ) | BIT( FlagPMHotSideBlocked ) );

    bool cmd_on   = ( pmStatus & BIT( FlagPMHotSideEnabled ) );
    bool ac_live  = ( pmStatus & BIT( FlagPMSwitchedACLive ) );
    bool tripped  = ( pmStatus & BIT( FlagPMEStopTripped ) );

    if ( !cmd_on && ac_live ) {
        pmStatus |= BIT( FlagPMHotSideRogue );
    } else if ( cmd_on && !ac_live && !tripped ) {
        pmStatus |= BIT( FlagPMHotSideDead );
    } else if ( cmd_on && tripped ) {
        pmStatus |= BIT( FlagPMHotSideBlocked );
    }

    // Raise global fault if hardware is in an illegal state.
    if ( pmStatus & ( BIT( FlagPMHotSideRogue ) | BIT( FlagPMHotSideDead ) ) ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagPowerManagerFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagPowerManagerFault ) );
    }

    // Push full telemetry for the ManagerTask.
    osEventFlagsSet( PowerManagerStatusFlagsHandle, pmStatus );
}

bool PMEnableHotSide( void ) {
    if ( ( pmStatus & BIT( FlagPMMainsPower ) ) && !( pmStatus & BIT( FlagPMEStopTripped ) ) ) {
        pmStatus |= BIT( FlagPMHotSideEnabled );
        HAL_GPIO_WritePin( HOT_SIDE_PWR_EN_N_GPIO_Port, HOT_SIDE_PWR_EN_N_Pin, GPIO_PIN_RESET );
        return true;
    }
    return false;
}

bool PMDisableHotSide( void ) {
    pmStatus &= ~BIT( FlagPMHotSideEnabled );
    HAL_GPIO_WritePin( HOT_SIDE_PWR_EN_N_GPIO_Port, HOT_SIDE_PWR_EN_N_Pin, GPIO_PIN_SET );
    return true;
}

bool PMEnableAuxPower( void ) {
    pmStatus |= BIT( FlagPMAuxPowerEnabled );
    HAL_GPIO_WritePin( AUX_24V_EN_N_GPIO_Port, AUX_24V_EN_N_Pin, GPIO_PIN_RESET );
    return true;
}

bool PMDisableAuxPower( void ) {
    pmStatus &= ~BIT( FlagPMAuxPowerEnabled );
    HAL_GPIO_WritePin( AUX_24V_EN_N_GPIO_Port, AUX_24V_EN_N_Pin, GPIO_PIN_SET );
    return true;
}

uint32_t PMGetStatus( void ) {
    return pmStatus;
}

void PMHandleZCDInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    lastZCDTick = HAL_GetTick();
    pmStatus |= BIT( FlagPMSwitchedACLive );
}

void PMHandleEStopInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    PMDisableHotSide();
    pmStatus |= BIT( FlagPMEStopTripped );
    osEventFlagsSet( FaultFlagsHandle, BIT( FlagESTOP ) );
}