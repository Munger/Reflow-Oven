/// @file ManagerTask.c
///
/// @brief Manager task — driver initialisation sequencer and fault supervisor.
///
/// Calls SPIInitModule() and I2CInitModule() to create bus semaphores, then calls
/// each driver's InitModule() for alloc-only setup (RTOS handles, pin assignments —
/// no hardware I/O). Device Open() calls are the responsibility of each consuming
/// module: OvenController opens its own thermocouples and thermistor, Reflow opens
/// OvenController and ACFan, DeviceTask opens remaining peripherals. ManagerTask
/// waits for DEVICE_ALL_READY once those tasks have run their init, then enables
/// GPIO interrupts and broadcasts FlagSystemInitialised.
///
/// After startup the loop blocks on any FaultFlagsHandle bit for supervisory response.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"
#include "ManagerTask.h"
#include "MCU.h"
#include "OvenController.h"
#include "PowerManager.h"
#include "SPIManager.h"
#include "I2CManager.h"
#include "Triac.h"
#include "Buzzer.h"
#include "Thermistor.h"
#include "ThermistorI2C.h"
#include "Thermocouple.h"
#include "DCFan.h"
#include "RotaryEncoder.h"
#include "ACFan.h"
#include "ACLight.h"
#include "USBPowerDelivery.h"
#include "Flash.h"

/// @brief Initialise all driver modules in dependency order, then signal system readiness.
///
/// Phase 1 — bus managers: create SPI and I2C semaphores. Must precede any driver
///            that calls SPIOpen() or I2COpen() internally.
/// Phase 2 — all driver InitModule() calls. Each sets its own ready bit in
///            DeviceStatusFlagsHandle on completion, satisfying DEVICE_ALL_READY.
/// Phase 3 — wait for DEVICE_ALL_READY, enable GPIO interrupts, broadcast FlagSystemInitialised.
void ManagerTaskInit( void ) {

    // Bus managers
#if FEATURE_THERMOCOUPLES || FEATURE_FLASH
    SPIInitModule();
#endif // FEATURE_THERMOCOUPLES || FEATURE_FLASH

#if FEATURE_BOARD_FAN || FEATURE_ROTARY_ENCODER || FEATURE_THERMISTOR_HEATSINK || FEATURE_USB_PD
    I2CInitModule();
#endif // FEATURE_BOARD_FAN || FEATURE_ROTARY_ENCODER || FEATURE_THERMISTOR_HEATSINK || FEATURE_USB_PD

    // Alloc-only driver init (no hardware access)
    PMInitModule();
    MCUInitModule();
    TriacInitModule();
    OCInitModule();

#if FEATURE_BUZZER
    BuzzerInitModule();
#endif // FEATURE_BUZZER

#if FEATURE_THERMISTORS
    TMInitModule();
#endif // FEATURE_THERMISTORS

#if FEATURE_THERMOCOUPLES
    TCInitModule();
#endif // FEATURE_THERMOCOUPLES

#if FEATURE_THERMISTOR_HEATSINK
    TMI2CInitModule();
#endif // FEATURE_THERMISTOR_HEATSINK

#if FEATURE_BOARD_FAN
    DCFanInitModule();
#endif // FEATURE_BOARD_FAN

#if FEATURE_ROTARY_ENCODER
    REInitModule();
#endif // FEATURE_ROTARY_ENCODER

#if FEATURE_OVEN_FAN
    ACFanInitModule();
#endif // FEATURE_OVEN_FAN

#if FEATURE_USB_PD
    USBPDInitModule();
#endif // FEATURE_USB_PD

#if FEATURE_OVEN_LIGHT
    ACLightInitModule();
#endif // FEATURE_OVEN_LIGHT

#if FEATURE_FLASH
    FlashInitModule();
#endif // FEATURE_FLASH

    // Wait for all devices, enable interrupts, signal init complete
    osEventFlagsWait( DeviceStatusFlagsHandle, DEVICE_ALL_READY, osFlagsWaitAll | osFlagsNoClear, osWaitForever );
    osEventFlagsSet( SystemStatusFlagsHandle, BIT( FlagInterruptsEnabled ) );
    osEventFlagsSet( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ) );
}

/// @brief Supervisor loop — blocks on any active fault and reacts accordingly.
///
/// Currently captures the fault bitmask but does not implement a reaction policy.
/// Fault handling (safe-state enforcement, buzzer alert, etc.) is TBD.
void ManagerTaskLoop( void ) {
    uint32_t faults = osEventFlagsWait( FaultFlagsHandle, FAULT_ANY, osFlagsWaitAny | osFlagsNoClear, osWaitForever );
    (void)faults;
}
