#ifndef USBPOWERDELIVERY_H
#define USBPOWERDELIVERY_H

#include <stdint.h>
#include "types.h"

// Current power role of the USB-C port.
typedef enum {
    USBPDRoleNone = 0,
    USBPDRoleSink,      // We are consuming power from the port
    USBPDRoleSource     // We are providing power to a guest device
} USBPDPowerRole;

// Detailed status of the Type-C connection and PD policy engine.
typedef enum {
    USBPDStatusDisabled = 0,
    USBPDStatusDisconnected,
    USBPDStatusConnecting,
    USBPDStatusNegotiating,
    USBPDStatusContractReady,
    USBPDStatusError
} USBPDStatus;

typedef struct {
    Voltage  voltage;
    Current  maxCurrent;
    Power    maxPower;
} USBPDPowerProfile, *USBPDPowerProfilePtr;

// Initialise UCPD, STPD01 buck, and TCPP03 protection.
void              USBPDInitModule( void );

// Returns the current status of the connection.
USBPDStatus       USBPDGetStatus( void );

// Returns whether we are currently a Sink or a Source.
USBPDPowerRole    USBPDGetPowerRole( void );

// Returns the number of power profiles offered by the partner.
uint8_t           USBPDGetProfileCount( void );

// Retrieves a specific profile offered by the partner.
USBPDPowerProfile USBPDGetProfile( uint8_t index );

// Request a voltage (as Sink) or set an output limit (as Source).
void              USBPDRequestVoltage( Voltage target );

// Get the actual live voltage and current on VBUS.
Voltage           USBPDGetLiveVoltage( void );
Current           USBPDGetLiveCurrent( void );

// Background task to handle I2C telemetry from STPD01/TCPP03 and PD logic.
void              USBPDProcess( void );

// Interrupt handlers
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin );
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin );

#endif // USBPOWERDELIVERY_H