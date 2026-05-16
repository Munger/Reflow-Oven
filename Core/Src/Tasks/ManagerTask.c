/// @file ManagerTask.c
///
/// @brief Manager task — driver initialisation sequencer and fault supervisor.
///
/// Calls every driver's InitModule() in the correct dependency order, then
/// waits for DEVICE_ALL_READY before enabling GPIO interrupts and broadcasting
/// FlagSystemInitialised. After startup, the loop blocks on any FaultFlagsHandle
/// bit for supervisory response.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "ManagerTask.h"
#include "Buzzer.h"
#include "DCFan.h"
#include "MCU.h"
#include "PowerManager.h"
#include "RotaryEncoder.h"
#include "Thermistor.h"
#include "ThermistorI2C.h"
#include "Thermocouple.h"
#include "Triac.h"
#include "USBPowerDelivery.h"

/// @brief Initialise all driver modules in dependency order, then signal system readiness.
///
/// Each XxxInitModule() call creates the module's private event flag group and
/// configures the associated hardware. After all modules set their Ready bits in
/// DeviceStatusFlagsHandle, this function enables GPIO interrupts and broadcasts
/// FlagSystemInitialised so that all waiting tasks can proceed.
void ManagerTaskInit( void ) {
    PMInitModule();
    MCUInitModule();
    BuzzerInitModule();
    DCFanInitModule();
    REInitModule();
    TMInitModule();
    TMI2CInitModule();
    TCInitModule();
    USBPDInitModule();
    TriacInitModule();
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
