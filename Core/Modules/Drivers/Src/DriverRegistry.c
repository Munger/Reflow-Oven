/// @file DriverRegistry.c
///
/// @brief Central driver lifecycle registry table implementation.
///
/// One `const` array to rule them all. Every driver that exposes InitModule
/// and/or Process lives here. Feature guards prevent dead entries on boards
/// where the hardware isn't fitted.
///
/// Order matters: bus managers (SPI, I2C) come first so their semaphores
/// exist before any driver that calls SPIOpen() or I2COpen(). The remaining
/// entries are ordered to minimise inter-module latency in the DeviceTask
/// process loop: power and health, then sensors, then actuators, then the
/// oven control loop.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "DriverRegistry.h"

#include "Features.h"
#include "SPIManager.h"
#include "I2CManager.h"
#include "USBCDC.h"
#include "PowerManager.h"
#include "MCU.h"
#include "Triac.h"
#include "OvenController.h"

#if FEATURE_BUZZER
#include "Buzzer.h"
#endif

#if FEATURE_THERMISTORS
#include "Thermistor.h"
#endif

#if FEATURE_THERMISTOR_HEATSINK
#include "ThermistorI2C.h"
#endif

#if FEATURE_THERMOCOUPLES
#include "Thermocouple.h"
#endif

#if FEATURE_BOARD_FAN
#include "DCFan.h"
#endif

#if FEATURE_ROTARY_ENCODER
#include "RotaryEncoder.h"
#endif

#if FEATURE_OVEN_FAN
#include "ACFan.h"
#endif

#if FEATURE_USB_PD
#include "USBPowerDelivery.h"
#endif

#if FEATURE_OVEN_LIGHT
#include "ACLight.h"
#endif

#if FEATURE_FLASH
#include "Flash.h"
#endif

// ============================================================================
// Instance tables — name-to-ID mapping for each driver with named hardware
// ============================================================================

#if FEATURE_HEATER_TOP || FEATURE_HEATER_REAR || FEATURE_HEATER_BOTTOM || FEATURE_OVEN_FAN || FEATURE_OVEN_LIGHT
static const InstanceEntry kTriacInstances[] = {
#if FEATURE_HEATER_TOP
    { "top",    TriacHeaterTop    },
#endif
#if FEATURE_HEATER_REAR
    { "rear",   TriacHeaterRear   },
#endif
#if FEATURE_HEATER_BOTTOM
    { "bottom", TriacHeaterBottom },
#endif
#if FEATURE_OVEN_FAN
    { "ovenFan", TriacOvenFan     },
#endif
#if FEATURE_OVEN_LIGHT
    { "light",  TriacLight        },
#endif
};
#endif

#if FEATURE_THERMOCOUPLES
static const InstanceEntry kTCInstances[] = {
    { "oven", Thermocouple1 },
};
#endif

#if FEATURE_THERMISTORS
static const InstanceEntry kTMInstances[] = {
#if FEATURE_THERMISTOR_CJT_1
    { "cjt1",  ThermistorCJT1  },
#endif
#if FEATURE_THERMISTOR_CJT_2
    { "cjt2",  ThermistorCJT2  },
#endif
#if FEATURE_THERMISTOR_OVEN
    { "oven",  ThermistorOven  },
#endif
};
#endif

#if FEATURE_THERMISTOR_HEATSINK
static const InstanceEntry kTMI2CInstances[] = {
    { "heatsink", 0 },
};
#endif

#if FEATURE_OVEN_FAN
static const InstanceEntry kACFanInstances[] = {
    { "ovenFan", OvenFan },
    { "oven",    OvenFan },
};
#endif

#if FEATURE_BOARD_FAN
static const InstanceEntry kDCFanInstances[] = {
    { "board", BoardCoolingFan },
};
#endif

#if FEATURE_OVEN_LIGHT
static const InstanceEntry kACLightInstances[] = {
    { "oven", OvenLight1 },
};
#endif

static const InstanceEntry kOCInstances[] = {
    { "main", OvenController1 },
};

// ============================================================================
// Driver table
// ============================================================================

/// @brief Every driver on the board, in init/process order.
///
/// Bus managers (SPI, I2C) are first — their init creates the bus semaphores
/// that downstream Open() calls depend on. The rest follow in device-task
/// process order: power/health, then sensors, then actuators, then the
/// oven control loop.
static const DriverEntry kDriverTable[] = {

    // Bus managers — Phase 1 init only, no Process()
#if FEATURE_THERMOCOUPLES || FEATURE_FLASH
    { "spi",  TaskOwnerDevice, SPIInitModule,  NULL,               NULL,               0 },
#endif

#if FEATURE_BOARD_FAN || FEATURE_ROTARY_ENCODER || FEATURE_THERMISTOR_HEATSINK || FEATURE_USB_PD
    { "i2c",  TaskOwnerDevice, I2CInitModule,  NULL,               NULL,               0 },
#endif

    // Phase 2 — unconditional core drivers
    { "pm",       TaskOwnerDevice, PMInitModule,      PMProcess,        NULL,               0 },
    { "mcu",      TaskOwnerDevice, MCUInitModule,     MCUProcess,       NULL,               0 },
#if FEATURE_HEATER_TOP || FEATURE_HEATER_REAR || FEATURE_HEATER_BOTTOM || FEATURE_OVEN_FAN || FEATURE_OVEN_LIGHT
    { "triac",    TaskOwnerDevice, TriacInitModule,   TriacProcess,     kTriacInstances,    sizeof kTriacInstances / sizeof kTriacInstances[0] },
#endif
    { "oc",       TaskOwnerDevice, OCInitModule,      OCProcess,        kOCInstances,       sizeof kOCInstances / sizeof kOCInstances[0] },

    // Feature-gated drivers
#if FEATURE_BUZZER
    { "buzzer",   TaskOwnerDevice, BuzzerInitModule,  BuzzerProcess,    NULL,               0 },
#endif

#if FEATURE_THERMISTORS
    { "thermistor",  TaskOwnerDevice, TMInitModule,   TMProcess,        kTMInstances,       sizeof kTMInstances / sizeof kTMInstances[0] },
#endif

#if FEATURE_THERMISTOR_HEATSINK
    { "thermistorI2C", TaskOwnerDevice, TMI2CInitModule, TMI2CProcess, kTMI2CInstances,    sizeof kTMI2CInstances / sizeof kTMI2CInstances[0] },
#endif

#if FEATURE_THERMOCOUPLES
    { "tc",       TaskOwnerDevice, TCInitModule,      TCProcess,        kTCInstances,       sizeof kTCInstances / sizeof kTCInstances[0] },
#endif

#if FEATURE_BOARD_FAN
    { "dcFan",    TaskOwnerDevice, DCFanInitModule,   DCFanProcess,     kDCFanInstances,    sizeof kDCFanInstances / sizeof kDCFanInstances[0] },
#endif

#if FEATURE_ROTARY_ENCODER
    { "encoder",  TaskOwnerDevice, REInitModule,      REProcess,        NULL,               0 },
#endif

#if FEATURE_OVEN_FAN
    { "acFan",    TaskOwnerDevice, ACFanInitModule,   ACFanProcess,     kACFanInstances,    sizeof kACFanInstances / sizeof kACFanInstances[0] },
#endif

#if FEATURE_OVEN_LIGHT
    { "acLight",  TaskOwnerDevice, ACLightInitModule, ACLightProcess,   kACLightInstances,  sizeof kACLightInstances / sizeof kACLightInstances[0] },
#endif

#if FEATURE_USB_PD
    { "usbpd",    TaskOwnerUSBPD,  USBPDInitModule,   USBPDProcess,     NULL,               0 },
#endif

#if FEATURE_FLASH
    { "flash",    TaskOwnerDevice, FlashInitModule,   NULL,             NULL,               0 },
#endif

    // CDC serial driver — owns the full request-dispatch + transmit pipeline
    { "usbcdc",   TaskOwnerAPI,    USBCDCInitModule,   USBCDCProcess,   NULL,               0 },
};

// ============================================================================
// Instance lookup
// ============================================================================

uint16_t DriverFindInstance( const char* driverName, const char* instanceName ) {
    int count = 0;
    DriverEntryPtr table = DriverTable( &count );
    for ( int i = 0; i < count; i++ ) {
        if ( table[ i ].instances == NULL ) continue;
        // Simple name prefix match for the driver table entry itself
        const char* a = table[ i ].name;
        const char* b = driverName;
        while ( *a && *a == *b ) { a++; b++; }
        if ( *a != *b ) continue;
        // Found the driver — search its instance table
        for ( uint8_t j = 0; j < table[ i ].instanceCount; j++ ) {
            const char* x = table[ i ].instances[ j ].name;
            const char* y = instanceName;
            while ( *x && *x == *y ) { x++; y++; }
            if ( *x == '\0' && *y == '\0' ) {
                return table[ i ].instances[ j ].id;
            }
        }
        break;  // Driver matched but instance not found — stop here
    }
    return DRIVER_INSTANCE_NONE;
}

// ============================================================================
// Accessor
// ============================================================================

DriverEntryPtr DriverTable( int* count ) {
    if ( count ) *count = (int)( sizeof( kDriverTable ) / sizeof( kDriverTable[ 0 ] ) );
    return kDriverTable;
}
