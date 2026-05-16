/// @file USBPowerDelivery.h
///
/// @brief USB Power Delivery driver for STPD01 buck converter and TCPP03 protection IC.
///
/// Manages the USB-C port as either a Sink (drawing power) or Source (supplying power).
/// USBPDProcess() handles role detection, voltage negotiation, and telemetry refresh.
/// Getters return cached values safe from any task context. ISR handlers only set
/// volatile flags; I2C autopsy is deferred to USBPDProcess().
///
/// Call USBPDGetStatus() to retrieve raw diagnostic bits. Decode them using the
/// USBPDStatusBit enum — FlagUSBPDRoleSource distinguishes role when the port is
/// connected (1 = Source, 0 = Sink).
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef USBPOWERDELIVERY_H
#define USBPOWERDELIVERY_H

#include <stdint.h>
#include "Types.h"
#include "SystemStatusFlags.h"
#include "I2CManager.h"

/// @brief Logical identifiers for USBPD instances managed by this driver.
typedef enum {
    USBPD1 = 0,   ///< USB-C port with STPD01 + TCPP03
    USBPDCount
} USBPDID;

/// @brief Diagnostic flag bit positions for the USBPD driver status event group.
///
/// Returned by USBPDGetStatus(). FlagUSBPDRoleSource is only meaningful when
/// FlagUSBPDConnected is also set (0 = Sink, 1 = Source).
typedef enum {
    FlagUSBPDModuleReady = 0,    ///< InitModule completed successfully.
    FlagUSBPDConnected,          ///< A USB-C cable or device is present.
    FlagUSBPDContractActive,     ///< PD contract established; power is flowing.
    FlagUSBPDRoleSource,         ///< Port is operating as a Source (0 = Sink when connected).
    FlagUSBPDFaultDetected,      ///< OVP, short, or PD protocol fault is active.
    FlagUSBPDVoltagePending,     ///< A voltage change request is queued for the next tick.
    FlagUSBPDSourceFaultPending, ///< STPD01 fault ISR fired; I2C autopsy pending in USBPDProcess().

    USBPDStatusFlagsCount        ///< Must stay <= 24.
} USBPDStatusBit;

_Static_assert( USBPDStatusFlagsCount <= 24, "USBPDStatusFlags out of bounds" );

/// @brief Power profile entry describing a single voltage/current capability.
typedef struct {
    Voltage  voltage;    ///< Profile voltage in millivolts.
    Current  maxCurrent; ///< Maximum current in milliamps.
    Power    maxPower;   ///< Maximum power in milliwatts.
} USBPDPowerProfile, *USBPDPowerProfilePtr;

/// @brief Opaque handle to a USBPD instance.
typedef struct USBPDInstance* USBPDRef;

/// @brief Allocate per-instance resources and enable the UCPD peripheral.
/// Does not access I2C hardware — that happens in USBPDOpen().
void              USBPDInitModule( void );

/// @brief Open a handle to a specific USBPD instance and configure the TCPP03.
///
/// On first call for a given ID: stores @p i2c, writes the TCPP03 configuration and OVP
/// threshold, and signals DeviceStatusFlagsHandle. Subsequent calls with the same ID
/// return the existing instance without re-configuring.
///
/// @param[in] id  USBPD instance identifier.
/// @param[in] i2c I2C bus handle returned by I2COpen().
/// @return Handle to the instance, or NULL if @p id is out of range.
USBPDRef          USBPDOpen( USBPDID id, I2CRef i2c );

/// @brief Return the raw diagnostic flag bits for system snapshots and status queries.
/// @param[in] pd Handle returned by USBPDOpen().
/// @return Bitmask of USBPDStatusBit values; 0 if @p pd is NULL.
uint32_t          USBPDGetStatus( USBPDRef pd );

/// @brief Return the number of power profiles available.
///
/// Returns 4 when operating as a Source (locally defined profiles), or the number
/// of profiles advertised by the partner when operating as a Sink.
///
/// @param[in] pd Handle returned by USBPDOpen().
/// @return Profile count; safe to call from any task context.
uint8_t           USBPDGetProfileCount( USBPDRef pd );

/// @brief Return a specific power profile by index.
/// @param[in] pd     Handle returned by USBPDOpen().
/// @param[in] index  Zero-based index into the profile list.
/// @return The requested profile, or a zero-initialised struct if out of range.
USBPDPowerProfile USBPDGetProfile( USBPDRef pd, uint8_t index );

/// @brief Queue a voltage request; I2C writes are applied by USBPDProcess() on the next tick.
/// @param[in] pd      Handle returned by USBPDOpen().
/// @param[in] target  Requested voltage in millivolts.
void              USBPDRequestVoltage( USBPDRef pd, Voltage target );

/// @brief Return the most recently measured VBUS voltage.
/// @param[in] pd Handle returned by USBPDOpen().
/// @return Cached VBUS voltage in millivolts; refreshed by USBPDProcess() each tick.
Voltage           USBPDGetLiveVoltage( USBPDRef pd );

/// @brief Return the most recently measured VBUS current.
/// @param[in] pd Handle returned by USBPDOpen().
/// @return Cached VBUS current in milliamps; refreshed by USBPDProcess() each tick.
Current           USBPDGetLiveCurrent( USBPDRef pd );

/// @brief Drive the PD policy engine, apply voltage requests, and refresh telemetry.
///
/// Determines the role from the MAINS_PWR_N GPIO, handles deferred fault autopsy,
/// applies any pending voltage request, refreshes cached VBUS telemetry, and
/// synchronises the private diagnostic event flags.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void              USBPDProcess( void );

/// @brief FLAG_N falling-edge ISR handler — disables the buck immediately on fault.
/// @param[in] GPIO_Pin  HAL pin mask (unused).
/// @warning ISR context. GPIO write only; I2C fault autopsy deferred to USBPDProcess().
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin );

/// @brief STPD01 source-interrupt falling-edge ISR handler — flags a fault for deferred handling.
/// @param[in] GPIO_Pin  HAL pin mask (unused).
/// @warning ISR context. Sets a volatile flag only; I2C status read deferred to USBPDProcess().
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin );

#endif // USBPOWERDELIVERY_H
