/// @file USBPowerDelivery.h
///
/// @brief USB Power Delivery driver for STPD01 buck converter and TCPP03 protection IC.
///
/// Manages the USB-C port as either a Sink (drawing power) or Source (supplying power).
/// USBPDProcess() handles role detection, voltage negotiation, and telemetry refresh.
/// Getters return cached values safe from any task context. ISR handlers only set
/// volatile flags; I2C autopsy is deferred to USBPDProcess().
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef USBPOWERDELIVERY_H
#define USBPOWERDELIVERY_H

#include <stdint.h>
#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Current power role of the USB-C port.
typedef enum {
    USBPDRoleNone = 0,   ///< Port is not connected or role is undetermined
    USBPDRoleSink,        ///< We are consuming power from the connected source
    USBPDRoleSource       ///< We are providing power to a connected device
} USBPDPowerRole;

/// @brief High-level status of the Type-C connection and PD policy engine.
typedef enum {
    USBPDStatusDisabled = 0,    ///< UCPD peripheral is not enabled
    USBPDStatusDisconnected,     ///< No cable or device detected
    USBPDStatusConnecting,       ///< Cable detected; waiting for partner advertisement
    USBPDStatusNegotiating,      ///< PD capabilities exchange in progress
    USBPDStatusContractReady,    ///< PD contract established; power is flowing
    USBPDStatusError             ///< Fault detected (OVP, short, or PD protocol error)
} USBPDStatus;

/// @brief Power profile entry describing a single voltage/current capability.
typedef struct {
    Voltage  voltage;     ///< Profile voltage in millivolts
    Current  maxCurrent;  ///< Maximum current in milliamps
    Power    maxPower;    ///< Maximum power in milliwatts
} USBPDPowerProfile, *USBPDPowerProfilePtr;

/// @brief Initialise UCPD, STPD01 buck converter, and TCPP03 protection IC.
///
/// Creates an internal recursive mutex for I2C access, enables the UCPD1 peripheral,
/// configures the TCPP03 OVP threshold, and signals FlagUSBPDReady.
void              USBPDInitModule( void );

/// @brief Return the current high-level connection status.
/// @return Cached USBPDStatus value; safe to call from any task context.
USBPDStatus       USBPDGetStatus( void );

/// @brief Return the current power role of the USB-C port.
/// @return Cached USBPDPowerRole value; safe to call from any task context.
USBPDPowerRole    USBPDGetPowerRole( void );

/// @brief Return the number of power profiles available.
///
/// As a Source, returns 4 (the locally defined profiles).
/// As a Sink, returns the number of profiles advertised by the partner.
///
/// @return Profile count; safe to call from any task context.
uint8_t           USBPDGetProfileCount( void );

/// @brief Return a specific power profile by index.
/// @param[in] index Zero-based index into the profile list.
/// @return The requested profile, or a zero-initialised struct if out of range.
USBPDPowerProfile USBPDGetProfile( uint8_t index );

/// @brief Queue a voltage request; I2C writes are applied by USBPDProcess() on the next tick.
/// @param[in] target Requested voltage in millivolts.
/// @note The critical section inside ensures atomicity of the multi-field pending write.
void              USBPDRequestVoltage( Voltage target );

/// @brief Return the most recently measured VBUS voltage.
/// @return Cached VBUS voltage in millivolts; refreshed by USBPDProcess() each tick.
/// @note Safe to call from any task context without blocking.
Voltage           USBPDGetLiveVoltage( void );

/// @brief Return the most recently measured VBUS current.
/// @return Cached VBUS current in milliamps; refreshed by USBPDProcess() each tick.
/// @note Safe to call from any task context without blocking.
Current           USBPDGetLiveCurrent( void );

/// @brief Drive the PD policy engine, apply voltage requests, and refresh telemetry.
///
/// Determines the role from the MAINS_PWR_N GPIO, handles deferred fault
/// autopsy (I2C read deferred from ISR), applies any pending voltage request,
/// and refreshes cached VBUS voltage and current from the TCPP03.
///
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void              USBPDProcess( void );

/// @brief FLAG_N falling-edge ISR handler — disables the buck immediately on over-voltage/fault.
/// @param[in] GPIO_Pin HAL pin mask (unused).
/// @warning ISR context. GPIO write only; I2C fault autopsy deferred to USBPDProcess().
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin );

/// @brief STPD01 source-interrupt falling-edge ISR handler — flags a fault for deferred handling.
/// @param[in] GPIO_Pin HAL pin mask (unused).
/// @warning ISR context. Sets a volatile flag only; I2C status read deferred to USBPDProcess().
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin );

#endif // USBPOWERDELIVERY_H
