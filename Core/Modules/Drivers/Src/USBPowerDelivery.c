/// @file USBPowerDelivery.c
///
/// @brief USB Power Delivery driver — STPD01 and TCPP03 I2C implementation.
///
/// Manages the UCPD peripheral and two I2C companion ICs (STPD01 buck converter
/// and TCPP03 USB-C protection). Role and fault state are held exclusively in the
/// private statusHandle event group — the flags ARE the state; there is no
/// separate shadow. Voltage requests are queued via FlagUSBPDVoltagePending and
/// applied inside USBPDProcess() to avoid blocking the caller. ISR handlers call
/// osEventFlagsSet() only, which is ISR-safe under CMSIS-RTOS2; all I2C access
/// is deferred to USBPDProcess() via I2CWriteSync / I2CReadSync.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_USB_PD

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "event_groups.h"
#include "main.h"
#include "I2CAddress.h"
#include "I2CManager.h"
#include "USBPowerDelivery.h"

// ============================================================================
// TCPP03 I2C device address and register map
// ============================================================================

static const uint16_t kTcpp03Addr    = (uint16_t)I2CAddrTCPP03 << 1;
static const uint8_t  kTcpp03Conf1   = 0x00U; ///< Configuration register 1.
static const uint8_t  kTcpp03OvpSet  = 0x01U; ///< Over-voltage protection threshold.
static const uint8_t  kTcpp03VsenseH = 0x08U; ///< VBUS voltage sense MSB.
static const uint8_t  kTcpp03IsenseH = 0x0AU; ///< VBUS current sense MSB.

// ============================================================================
// STPD01 I2C device address and register map
// ============================================================================

static const uint16_t kStpd01Addr      = (uint16_t)I2CAddrSTPD01 << 1;
static const uint8_t  kStpd01Vsel      = 0x00U; ///< Voltage selection register.
static const uint8_t  kStpd01StatusReg = 0x02U; ///< Status / fault register.

static const uint8_t kVbusScaleFactor = 5U; ///< Converts TCPP03 VSENSE reading to millivolts.
static const uint8_t kIbusScaleFactor = 2U; ///< Converts TCPP03 ISENSE reading to milliamps.

/// @brief Maximum partner PDOs stored during Sink negotiation.
enum { kMaxPartnerProfiles = 7 };

/// @brief Internal per-instance state for the USBPD driver.
typedef struct USBPDInstance {
    USBPDID           id;                                      ///< Instance identifier
    I2CRef            i2c;                                     ///< I2C bus handle acquired at USBPDOpen()
    osEventFlagsId_t  statusHandle;                            ///< Private diagnostic event flag group
    StaticEventGroup_t statusBuffer;                           ///< Storage backing statusHandle (no-heap allocation)
    USBPDPowerProfile activeProfile;                           ///< Currently negotiated or applied profile
    Voltage           cachedVoltage;                           ///< Last measured VBUS voltage (millivolts)
    Current           cachedCurrent;                           ///< Last measured VBUS current (milliamps)
    volatile Voltage  pendingVoltage;                          ///< Target voltage queued by USBPDRequestVoltage()
    USBPDPowerProfile partnerProfiles[ kMaxPartnerProfiles ]; ///< Profiles received during Sink negotiation
    uint8_t           partnerProfileCount;                     ///< Valid entries in partnerProfiles
} USBPDInstance, *USBPDInstancePtr;

/// @brief All USBPD instances — indexed by USBPDID.
static USBPDInstance instances[ USBPDCount ];

/// @brief Source profiles offered when operating as a USB-C power source.
/// Four fixed profiles: 5 V, 9 V, 12 V, 20 V — all at 15 W.
static const USBPDPowerProfile localSourceProfiles[] = {
    { 5000, 3000, 15000 }, { 9000, 1670, 15000 }, { 12000, 1250, 15000 }, { 20000, 750, 15000 }
};

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate per-instance resources and enable the UCPD peripheral.
///
/// Creates per-instance status event flag groups, enables the UCPD1 peripheral,
/// and de-asserts the PD_SRC_PON pin (source output off). Does not access I2C
/// hardware — that happens in USBPDOpen() once a bus reference is available.
void USBPDInitModule( void ) {
    instances[ USBPD1 ].id           = USBPD1;
    instances[ USBPD1 ].statusHandle = osEventFlagsNew( &(osEventFlagsAttr_t){ .cb_mem = &instances[ USBPD1 ].statusBuffer, .cb_size = sizeof( StaticEventGroup_t ) } );

    LL_UCPD_Enable( UCPD1 );
    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagUSBPDReady ) );
}

/// @brief Open a handle to a specific USBPD instance and configure the TCPP03.
///
/// On first call for a given ID: stores @p i2c, writes the TCPP03 configuration
/// and OVP threshold, and signals DeviceStatusFlagsHandle. Subsequent calls with
/// the same ID return the existing instance without re-configuring.
///
/// @param[in] id  USBPD instance identifier.
/// @param[in] i2c I2C bus handle returned by I2COpen().
/// @return Handle to the instance, or NULL if @p id is out of range.
USBPDRef USBPDOpen( USBPDID id, I2CRef i2c ) {
    if ( id >= USBPDCount ) return NULL;
    USBPDInstancePtr inst = &instances[ id ];
    if ( inst->i2c == NULL ) {
        inst->i2c = i2c;
        uint8_t cfg = 0x01;
        I2CWriteSync( i2c, kTcpp03Addr, kTcpp03Conf1, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 100 );
        uint8_t ovp = 0x24;
        I2CWriteSync( i2c, kTcpp03Addr, kTcpp03OvpSet, I2C_MEMADD_SIZE_8BIT, &ovp, 1, 100 );
        osEventFlagsSet( inst->statusHandle, BIT( FlagUSBPDModuleReady ) );
    }
    return inst;
}

/// @brief FLAG_N falling-edge ISR handler — disable the buck immediately on fault.
///
/// De-asserts PD_SRC_PON to shut down the source output and sets FlagUSBPDFaultDetected.
/// I2C fault autopsy is deferred to USBPDProcess(). FaultFlagsHandle is synced by
/// USBPDProcess() on the next tick.
/// @warning ISR context. GPIO write and osEventFlagsSet() only — no I2C access.
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
    osEventFlagsSet( instances[ USBPD1 ].statusHandle, BIT( FlagUSBPDFaultDetected ) );
}

/// @brief STPD01 source-interrupt falling-edge ISR handler — flag for deferred I2C read.
///
/// Sets FlagUSBPDSourceFaultPending so that USBPDProcess() reads the STPD01 status
/// register on the next tick. The I2C read cannot happen in ISR context.
/// @warning ISR context. osEventFlagsSet() only — no I2C access.
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin ) {
    UNUSED( GPIO_Pin );
    osEventFlagsSet( instances[ USBPD1 ].statusHandle, BIT( FlagUSBPDSourceFaultPending ) );
}

/// @brief Drive the PD policy engine, apply voltage requests, and refresh telemetry.
///
/// Each tick:
///   1. Reads MAINS_PWR_N and updates role flags and UCPD role registers on transition.
///   2. Performs deferred STPD01 fault autopsy if FlagUSBPDSourceFaultPending is set.
///   3. Applies any pending voltage request via STPD01 and TCPP03 I2C writes.
///   4. Refreshes cachedVoltage and cachedCurrent from the TCPP03.
///   5. Syncs FaultFlagsHandle from FlagUSBPDFaultDetected.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void USBPDProcess( void ) {
    USBPDInstancePtr inst = &instances[ USBPD1 ];
    if ( inst->i2c == NULL ) return;

    uint32_t status    = osEventFlagsGet( inst->statusHandle );
    bool     isSource  = ( status & BIT( FlagUSBPDRoleSource  ) ) != 0;
    bool     connected = ( status & BIT( FlagUSBPDConnected   ) ) != 0;

    GPIO_PinState mainsState = HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin );
    if ( mainsState == GPIO_PIN_SET ) {
        if ( isSource || !connected ) {
            osEventFlagsClear( inst->statusHandle, BIT( FlagUSBPDRoleSource ) );
            osEventFlagsSet( inst->statusHandle, BIT( FlagUSBPDConnected ) | BIT( FlagUSBPDContractActive ) );
            LL_UCPD_SetSNKRole( UCPD1 );
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
            isSource = false;
        }
    } else {
        if ( !isSource || !connected ) {
            osEventFlagsClear( inst->statusHandle, BIT( FlagUSBPDContractActive ) | BIT( FlagUSBPDFaultDetected ) );
            osEventFlagsSet( inst->statusHandle, BIT( FlagUSBPDRoleSource ) | BIT( FlagUSBPDConnected ) );
            LL_UCPD_SetSRCRole( UCPD1 );
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_SET );
            isSource = true;
        }
    }

    if ( status & BIT( FlagUSBPDSourceFaultPending ) ) {
        osEventFlagsClear( inst->statusHandle, BIT( FlagUSBPDSourceFaultPending ) );
        uint8_t statusReg = 0;
        if ( I2CReadSync( inst->i2c, kStpd01Addr, kStpd01StatusReg, I2C_MEMADD_SIZE_8BIT, &statusReg, 1, 10 ) == HAL_OK ) {
            if ( statusReg & 0x03 ) {
                HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
                osEventFlagsSet( inst->statusHandle, BIT( FlagUSBPDFaultDetected ) );
            }
        }
    }

    if ( status & BIT( FlagUSBPDVoltagePending ) ) {
        osEventFlagsClear( inst->statusHandle, BIT( FlagUSBPDVoltagePending ) );
        Voltage target = inst->pendingVoltage;
        if ( isSource ) {
            uint8_t vsel   = (uint8_t)( ( target - 3000 ) / 20 );
            uint8_t ovpVal = (uint8_t)( ( target * 12 ) / 10000 );
            I2CWriteSync( inst->i2c, kStpd01Addr, kStpd01Vsel, I2C_MEMADD_SIZE_8BIT, &vsel, 1, 50 );
            I2CWriteSync( inst->i2c, kTcpp03Addr, kTcpp03OvpSet, I2C_MEMADD_SIZE_8BIT, &ovpVal, 1, 50 );
            inst->activeProfile.voltage = target;
        }
    }

    uint8_t  buf[ 2 ];
    uint16_t raw = 0;
    if ( I2CReadSync( inst->i2c, kTcpp03Addr, kTcpp03VsenseH, I2C_MEMADD_SIZE_8BIT, buf, 2, 20 ) == HAL_OK ) {
        raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
        inst->cachedVoltage = (Voltage)( raw * kVbusScaleFactor );
    }
    if ( I2CReadSync( inst->i2c, kTcpp03Addr, kTcpp03IsenseH, I2C_MEMADD_SIZE_8BIT, buf, 2, 20 ) == HAL_OK ) {
        raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
        inst->cachedCurrent = (Current)( raw * kIbusScaleFactor );
    }

    if ( osEventFlagsGet( inst->statusHandle ) & BIT( FlagUSBPDFaultDetected ) ) {
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagUSBPDFault ) );
    } else {
        osEventFlagsClear( FaultFlagsHandle, BIT( FlagUSBPDFault ) );
    }
}

/// @brief Return the raw diagnostic flag bits for this USBPD instance.
uint32_t USBPDGetStatus( USBPDRef pd ) {
    return pd ? osEventFlagsGet( pd->statusHandle ) : 0;
}

/// @brief Return the number of power profiles currently available.
uint8_t USBPDGetProfileCount( USBPDRef pd ) {
    if ( pd == NULL ) return 0;
    return ( osEventFlagsGet( pd->statusHandle ) & BIT( FlagUSBPDRoleSource ) )
           ? 4 : pd->partnerProfileCount;
}

/// @brief Return a specific power profile by index.
USBPDPowerProfile USBPDGetProfile( USBPDRef pd, uint8_t index ) {
    if ( pd != NULL ) {
        if ( osEventFlagsGet( pd->statusHandle ) & BIT( FlagUSBPDRoleSource ) ) {
            if ( index < 4 ) return localSourceProfiles[ index ];
        } else {
            if ( index < pd->partnerProfileCount ) return pd->partnerProfiles[ index ];
        }
    }
    return (USBPDPowerProfile){ 0, 0, 0 };
}

/// @brief Queue a voltage request for deferred application by USBPDProcess().
///
/// Writes pendingVoltage then sets FlagUSBPDVoltagePending. Sequential execution
/// on Cortex-M0+ guarantees the value is visible before the flag is observed.
void USBPDRequestVoltage( USBPDRef pd, Voltage target ) {
    if ( pd == NULL ) return;
    pd->pendingVoltage = target;
    osEventFlagsSet( pd->statusHandle, BIT( FlagUSBPDVoltagePending ) );
}

/// @brief Return the most recently measured VBUS voltage.
Voltage USBPDGetLiveVoltage( USBPDRef pd ) {
    return pd ? pd->cachedVoltage : 0;
}

/// @brief Return the most recently measured VBUS current.
Current USBPDGetLiveCurrent( USBPDRef pd ) {
    return pd ? pd->cachedCurrent : 0;
}

#endif // FEATURE_USB_PD
