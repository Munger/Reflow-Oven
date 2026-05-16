/// @file DeviceTask.c
///
/// @brief Device task — periodic driver process loop implementation.
///
/// DeviceTaskLoop() calls the Process() function of every hardware driver module
/// in a deterministic sequence on each tick. The order is chosen to minimise
/// inter-module latency: power management and MCU first, then sensors and actuators.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "DeviceTask.h"
#include "SystemStatusFlags.h"
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

/// @brief Initialise the Device task — waits for system initialisation before proceeding.
///
/// Blocks indefinitely on FlagSystemInitialised so that no driver Process() functions
/// run until all drivers have been initialised by ManagerTask.
void DeviceTaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );
}

/// @brief Call the Process() function for every driver module in sequence.
///
/// Each Process() function performs all hardware I/O, updates cached state, and
/// sets/clears status and fault flags for its respective module. No hardware access
/// occurs outside of these calls in this task.
void DeviceTaskLoop( void ) {
    PMProcess();
    MCUProcess();
    BuzzerProcess();
    DCFanProcess();
    REProcess();
    TMProcess();
    TMI2CProcess();
    TCProcess();
    USBPDProcess();
    TriacProcess();
}
