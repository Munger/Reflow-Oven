/// @file ManagerTask.c
///
/// @brief Manager task — driver initialisation sequencer and fault supervisor.
///
/// Opens SPI and I2C buses first, then calls each driver's InitModule() for
/// alloc-only setup, followed by XxxOpen() to bind hardware and configure
/// devices. Waits for DEVICE_ALL_READY before enabling GPIO interrupts and
/// broadcasting FlagSystemInitialised. After startup, the loop blocks on any
/// FaultFlagsHandle bit for supervisory response.
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
#include "SPIManager.h"
#include "I2CManager.h"
#include "Thermistor.h"
#include "ThermistorI2C.h"
#include "Thermocouple.h"
#include "Flash.h"
#include "Triac.h"
#include "USBPowerDelivery.h"

/// @brief Initialise all driver modules in dependency order, then signal system readiness.
///
/// Phase 1 — bus managers: SPIInitModule() and I2CInitModule() create bus semaphores.
/// Phase 2 — bus open: SPIOpen() and I2COpen() return refs used by all peripheral drivers.
/// Phase 3 — alloc-only InitModule() calls: create per-driver event flag groups and assign
///            static GPIO/pin mappings; no hardware I/O at this stage.
/// Phase 4 — device Open() calls: bind bus refs, write hardware config registers, and set
///            per-device Ready bits in DeviceStatusFlagsHandle.
/// Phase 5 — waits for DEVICE_ALL_READY, enables interrupts, and broadcasts FlagSystemInitialised.
void ManagerTaskInit( void ) {
    // Phase 1: bus managers
    SPIInitModule();
    I2CInitModule();

    // Phase 2: open bus handles
    SPIRef spi = SPIOpen( SPIBus1 );
    I2CRef i2c = I2COpen( I2CBus1 );

    // Phase 3: alloc-only driver init (no hardware access)
    PMInitModule();
    MCUInitModule();
    BuzzerInitModule();
    TMInitModule();
    TCInitModule();
    DCFanInitModule();
    REInitModule();
    TMI2CInitModule();
    USBPDInitModule();
    TriacInitModule();

    // Phase 4: open devices — sets Ready bits in DeviceStatusFlagsHandle
    MCUOpen( MCU0 );
    TMOpen( ThermistorOven );
    TCOpen( Thermocouple1, spi );   // also calls TMOpen(ThermistorCJT1) internally
    TCOpen( Thermocouple2, spi );   // also calls TMOpen(ThermistorCJT2) internally
    DCFanOpen( BoardCoolingFan, i2c );
    REOpen( RotaryEncoder1, i2c );
    TMI2COpen( ThermistorI2C1, i2c, NULL );
    USBPDOpen( USBPD1, i2c );
    FlashOpen( Flash1, spi );

    // Phase 5: wait for all devices, enable interrupts, signal init complete
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
