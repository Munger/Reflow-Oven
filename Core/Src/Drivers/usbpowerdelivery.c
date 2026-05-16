/// @file USBPowerDelivery.c
///
/// @brief USB Power Delivery driver — STPD01 and TCPP03 I2C implementation.
///
/// Manages the UCPD peripheral and two I2C companion ICs (STPD01 buck converter
/// and TCPP03 USB-C protection). Role is determined on each USBPDProcess() tick
/// from the MAINS_PWR_N GPIO. Voltage requests are queued by USBPDRequestVoltage()
/// and applied inside USBPDProcess() to avoid blocking the caller. ISR handlers
/// only write volatile flags or GPIO; all I2C access is deferred to USBPDProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "i2c.h"
#include "main.h"
#include "USBPowerDelivery.h"

/// @name TCPP03 I2C device address and register map
/// @{
#define TCPP03_ADDR       ( 0x68 << 1 )  ///< 7-bit address shifted left by 1
#define TCPP03_CONF1      0x00            ///< Configuration register 1
#define TCPP03_OVP_SET    0x01            ///< Over-voltage protection threshold
#define TCPP03_VSENSE_H   0x08            ///< VBUS voltage sense MSB
#define TCPP03_ISENSE_H   0x0A            ///< VBUS current sense MSB
/// @}

/// @name STPD01 I2C device address and register map
/// @{
#define STPD01_ADDR       ( 0x28 << 1 )  ///< 7-bit address shifted left by 1
#define STPD01_VSEL       0x00            ///< Voltage selection register
#define STPD01_EN         0x01            ///< Enable register
#define STPD01_STATUS_REG 0x02            ///< Status / fault register
/// @}

/// @brief Scaling factor to convert TCPP03 VSENSE reading to millivolts.
#define VBUS_SCALE_FACTOR 5

/// @brief Scaling factor to convert TCPP03 ISENSE reading to milliamps.
#define IBUS_SCALE_FACTOR 2

/// @brief Source profiles offered when operating as a USB-C power source.
/// Four fixed profiles: 5 V, 9 V, 12 V, 20 V — all at 15 W.
static const USBPDPowerProfile local_source_profiles[] = {
    { 5000, 3000, 15000 }, { 9000, 1670, 15000 }, { 12000, 1250, 15000 }, { 20000, 750, 15000 }
};

/// @brief Partner source profiles received during Sink negotiation (up to 7 PDOs).
static USBPDPowerProfile partner_source_profiles[ 7 ];

/// @brief Number of valid entries in partner_source_profiles.
static uint8_t partner_profile_count = 0;

/// @brief Internal PD handle — all state in one anonymous struct for clean encapsulation.
static struct {
    USBPDStatus       status;                ///< Current connection status
    USBPDPowerRole    role;                  ///< Current power role (Sink/Source/None)
    USBPDPowerProfile active_profile;        ///< Currently negotiated or applied profile
    osMutexId_t       i2c_mtx;              ///< Recursive mutex guarding I2C bus access
    Voltage           cachedVoltage;         ///< Last measured VBUS voltage (millivolts)
    Current           cachedCurrent;         ///< Last measured VBUS current (milliamps)
    volatile Voltage  pendingVoltage;        ///< Target voltage queued by USBPDRequestVoltage()
    volatile bool     pendingVoltageRequest; ///< Set true when a voltage request is pending
    volatile bool     sourceFaultPending;    ///< Set by STPD01 ISR; cleared in USBPDProcess()
} pd_handle;

/// @brief Initialise UCPD, STPD01, TCPP03, and signal system readiness.
///
/// Creates a recursive mutex for I2C serialisation, enables the UCPD1 peripheral,
/// de-asserts the PD_SRC_PON pin (source output off), and configures the TCPP03
/// protection IC. Sets the initial state to Disconnected / RoleNone.
void USBPDInitModule( void ) {
    const osMutexAttr_t mtx_attr = { "pd_i2c_mtx", osMutexRecursive, NULL, 0 };
    pd_handle.i2c_mtx = osMutexNew( &mtx_attr );

    LL_UCPD_Enable( UCPD1 );

    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );

    if ( osMutexAcquire( pd_handle.i2c_mtx, osWaitForever ) == osOK ) {
        uint8_t cfg = 0x01;
        HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_CONF1, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 100 );
        uint8_t ovp = 0x24;
        HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_OVP_SET, I2C_MEMADD_SIZE_8BIT, &ovp, 1, 100 );
        osMutexRelease( pd_handle.i2c_mtx );
    }

    pd_handle.status = USBPDStatusDisconnected;
    pd_handle.role   = USBPDRoleNone;

    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagUSBPDReady ) );
}

/// @brief FLAG_N falling-edge ISR handler — disable the buck immediately on fault.
///
/// De-asserts PD_SRC_PON to shut down the source output. I2C fault autopsy is
/// deferred to USBPDProcess() to avoid I2C from ISR context.
///
/// @param[in] GPIO_Pin HAL pin mask (unused).
/// @warning ISR context. GPIO write only — no FreeRTOS blocking API.
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
    pd_handle.status = USBPDStatusError;
}

/// @brief STPD01 source-interrupt falling-edge ISR handler — flag for deferred I2C read.
///
/// Sets sourceFaultPending so that USBPDProcess() reads the STPD01 status register
/// on the next tick. The I2C read cannot happen here (ISR context).
///
/// @param[in] GPIO_Pin HAL pin mask (unused).
/// @warning ISR context. Sets a volatile bool only — no I2C access.
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    pd_handle.sourceFaultPending = true;
}

/// @brief Drive the PD policy engine, apply voltage requests, and refresh telemetry.
///
/// Each tick:
///   1. Determines the role from MAINS_PWR_N and updates UCPD role registers.
///   2. Performs deferred STPD01 fault autopsy if sourceFaultPending is set.
///   3. Applies any pending voltage request via STPD01 and TCPP03 I2C writes.
///   4. Refreshes cachedVoltage and cachedCurrent from the TCPP03.
///   5. Propagates USBPDStatusError to FaultFlagsHandle.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void USBPDProcess( void ) {
    // Role management — fast GPIO and UCPD register ops
    GPIO_PinState mains_state = HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin );
    if ( mains_state == GPIO_PIN_SET ) {
        if ( pd_handle.role != USBPDRoleSink ) {
            pd_handle.role = USBPDRoleSink;
            LL_UCPD_SetSNKRole( UCPD1 );
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
            pd_handle.status = USBPDStatusContractReady;
        }
    } else {
        if ( pd_handle.role != USBPDRoleSource ) {
            pd_handle.role = USBPDRoleSource;
            LL_UCPD_SetSRCRole( UCPD1 );
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_SET );
            pd_handle.status = USBPDStatusConnecting;
        }
    }

    // Deferred source fault autopsy — I2C read deferred from ISR to here
    if ( pd_handle.sourceFaultPending ) {
        pd_handle.sourceFaultPending = false;
        uint8_t status_reg = 0;
        if ( HAL_I2C_Mem_Read( &hi2c1, STPD01_ADDR, STPD01_STATUS_REG, 1, &status_reg, 1, 10 ) == HAL_OK ) {
            if ( status_reg & 0x03 ) {
                HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
                pd_handle.status = USBPDStatusError;
            }
        }
    }

    // Apply pending voltage request
    if ( pd_handle.pendingVoltageRequest ) {
        pd_handle.pendingVoltageRequest = false;
        Voltage target = pd_handle.pendingVoltage;
        if ( pd_handle.role == USBPDRoleSource ) {
            if ( osMutexAcquire( pd_handle.i2c_mtx, 100 ) == osOK ) {
                uint8_t vsel    = (uint8_t)( ( target - 3000 ) / 20 );
                uint8_t ovp_val = (uint8_t)( ( target * 12 ) / 10000 );
                HAL_I2C_Mem_Write( &hi2c1, STPD01_ADDR, STPD01_VSEL, I2C_MEMADD_SIZE_8BIT, &vsel, 1, 50 );
                HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_OVP_SET, I2C_MEMADD_SIZE_8BIT, &ovp_val, 1, 50 );
                pd_handle.active_profile.voltage = target;
                osMutexRelease( pd_handle.i2c_mtx );
            }
        }
    }

    // Refresh cached VBUS telemetry from the TCPP03
    if ( osMutexAcquire( pd_handle.i2c_mtx, 5 ) == osOK ) {
        uint8_t  buf[ 2 ];
        uint16_t raw = 0;
        if ( HAL_I2C_Mem_Read( &hi2c1, TCPP03_ADDR, TCPP03_VSENSE_H, I2C_MEMADD_SIZE_8BIT, buf, 2, 20 ) == HAL_OK ) {
            raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
            pd_handle.cachedVoltage = (Voltage)( raw * VBUS_SCALE_FACTOR );
        }
        if ( HAL_I2C_Mem_Read( &hi2c1, TCPP03_ADDR, TCPP03_ISENSE_H, I2C_MEMADD_SIZE_8BIT, buf, 2, 20 ) == HAL_OK ) {
            raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
            pd_handle.cachedCurrent = (Current)( raw * IBUS_SCALE_FACTOR );
        }
        osMutexRelease( pd_handle.i2c_mtx );
    }

    // Propagate fault state to global flags
    if ( pd_handle.status == USBPDStatusError ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagUSBPDFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagUSBPDFault ) );
    }
}

/// @brief Return the current high-level connection status.
/// @return Cached USBPDStatus; safe to call from any task context.
USBPDStatus USBPDGetStatus( void ) {
    return pd_handle.status;
}

/// @brief Return the current power role of the USB-C port.
/// @return Cached USBPDPowerRole; safe to call from any task context.
USBPDPowerRole USBPDGetPowerRole( void ) {
    return pd_handle.role;
}

/// @brief Return the number of power profiles currently available.
/// @return 4 when Source (local profiles); partner count when Sink.
uint8_t USBPDGetProfileCount( void ) {
    return ( pd_handle.role == USBPDRoleSource ) ? 4 : partner_profile_count;
}

/// @brief Return a specific power profile by index.
/// @param[in] index Zero-based index.
/// @return Requested profile, or zero-initialised struct if out of range.
USBPDPowerProfile USBPDGetProfile( uint8_t index ) {
    if ( pd_handle.role == USBPDRoleSource && index < 4 ) {
        return local_source_profiles[ index ];
    }
    if ( pd_handle.role == USBPDRoleSink && index < partner_profile_count ) {
        return partner_source_profiles[ index ];
    }
    return (USBPDPowerProfile){ 0, 0, 0 };
}

/// @brief Queue a voltage request for deferred application by USBPDProcess().
///
/// Writes pendingVoltage and pendingVoltageRequest atomically inside a critical
/// section so the two fields are never seen in an inconsistent state by USBPDProcess().
///
/// @param[in] target Requested voltage in millivolts.
void USBPDRequestVoltage( Voltage target ) {
    taskENTER_CRITICAL();
    pd_handle.pendingVoltage        = target;
    pd_handle.pendingVoltageRequest = true;
    taskEXIT_CRITICAL();
}

/// @brief Return the most recently measured VBUS voltage.
/// @return Cached voltage in millivolts; refreshed by USBPDProcess() each tick.
/// @note Safe to call from any task context without blocking.
Voltage USBPDGetLiveVoltage( void ) {
    return pd_handle.cachedVoltage;
}

/// @brief Return the most recently measured VBUS current.
/// @return Cached current in milliamps; refreshed by USBPDProcess() each tick.
/// @note Safe to call from any task context without blocking.
Current USBPDGetLiveCurrent( void ) {
    return pd_handle.cachedCurrent;
}
