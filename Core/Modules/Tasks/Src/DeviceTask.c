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

#include "Features.h"
#include "DeviceTask.h"
#include "MCU.h"
#include "OvenController.h"
#include "PowerManager.h"
#include "SystemStatusFlags.h"
#include "Buzzer.h"
#include "DCFan.h"
#include "RotaryEncoder.h"
#include "ACFan.h"
#include "Thermistor.h"
#include "ThermistorI2C.h"
#include "Thermocouple.h"
#include "Triac.h"

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
#if FEATURE_BUZZER
    BuzzerProcess();
#endif // FEATURE_BUZZER
#if FEATURE_BOARD_FAN
    DCFanProcess();
#endif // FEATURE_BOARD_FAN
#if FEATURE_ROTARY_ENCODER
    REProcess();
#endif // FEATURE_ROTARY_ENCODER
#if FEATURE_THERMISTORS
    TMProcess();
#endif // FEATURE_THERMISTORS
#if FEATURE_THERMISTOR_HEATSINK
    TMI2CProcess();
#endif // FEATURE_THERMISTOR_HEATSINK
#if FEATURE_THERMOCOUPLES
    TCProcess();
#endif // FEATURE_THERMOCOUPLES
    OCProcess();
#if FEATURE_OVEN_FAN
    ACFanProcess();
#endif // FEATURE_OVEN_FAN
    TriacProcess();
}
