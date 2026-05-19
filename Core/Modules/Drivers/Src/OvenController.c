/// @file OvenController.c
///
/// @brief Oven temperature regulator — implementation.
///
/// OCOpen() resolves all TRIAC channels and sensor handles internally using
/// hardcoded IDs — all devices are fixed on the board.
///
/// OCProcess() grabs the PB pointer atomically each tick, then reads mandate
/// fields directly through it, advances the ramp-limited setpoint, reads the
/// active temperature sources, drives the permitted TRIAC channels, and writes
/// currentTemp and state back into the caller's OvenControlPB.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

#include "Features.h"
#include "OvenController.h"
#include "Triac.h"
#include "Thermocouple.h"
#include "Thermistor.h"

/// @brief Safe oven cavity maximum in milli-degrees C; de-energises all heaters if exceeded.
static const Temperature kOverTempThreshold = 280000;

/// @brief Burst window in AC half-cycles used for proportional hold at target (200 ms at 50 Hz).
static const uint8_t kBurstWindow = 20U;

/// @brief Internal per-instance state for the oven controller.
typedef struct OvenControllerInstance {
    OvenControllerID  id;
    osEventFlagsId_t  statusHandle;
    OvenControlPBPtr  pb;           ///< Caller's PB; held for the duration of the run
    Temperature       rampTarget;   ///< Rate-limited intermediate setpoint
    uint32_t          lastTickMs;   ///< Kernel tick count at the last process call
#if FEATURE_HEATER_TOP
    TriacRef          triacTop;     ///< Top quartz heating bank
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
    TriacRef          triacRear;    ///< Rear convection element
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
    TriacRef          triacBottom;  ///< Bottom quartz heating bank
#endif // FEATURE_HEATER_BOTTOM
#if FEATURE_THERMOCOUPLE_1
    ThermocoupleRef   tc1;          ///< TC1 — board surface by wiring convention
#endif // FEATURE_THERMOCOUPLE_1
#if FEATURE_THERMOCOUPLE_2
    ThermocoupleRef   tc2;          ///< TC2 — free air at board level by wiring convention
#endif // FEATURE_THERMOCOUPLE_2
#if FEATURE_THERMISTOR_OVEN
    ThermistorRef     thermistor;   ///< Oven cavity thermistor, fixed at top of compartment
#endif // FEATURE_THERMISTOR_OVEN
} OvenControllerInstance, *OvenControllerInstancePtr;

static OvenControllerInstance instances[ OvenControllerCount ];

/// @brief Maps OvenControllerID to the corresponding DeviceStatusFlagsHandle ready bit.
static const uint8_t kReadyBit[ OvenControllerCount ] = {
    (uint8_t)FlagOvenControllerReady,
};

/// @brief Maps OvenControllerID to the corresponding FaultFlagsHandle fault bit.
static const uint8_t kFaultBit[ OvenControllerCount ] = {
    (uint8_t)FlagOvenControllerFault,
};

static Temperature SampleTemperature( OvenControllerInstancePtr inst );
static void        DriveHeaters( OvenControllerInstancePtr inst, OvenControlPBPtr pb, Permille power );
static bool        HeaterFaulted( OvenControllerInstancePtr inst, OvenControlPBPtr pb );

// ============================================================================
// Public API
// ============================================================================

/// @brief Allocate per-instance resources. Does not access hardware.
void OCInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );
    for ( uint8_t i = 0; i < OvenControllerCount; i++ ) {
        instances[ i ].id           = (OvenControllerID)i;
        instances[ i ].statusHandle = osEventFlagsNew( NULL );
        osEventFlagsSet( DeviceStatusFlagsHandle, BIT( kReadyBit[ i ] ) );
    }
#if FEATURE_HEATER_TOP
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagHeaterTopReady    ) );
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagHeaterRearReady   ) );
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagHeaterBottomReady ) );
#endif // FEATURE_HEATER_BOTTOM
}

/// @brief Open a handle to a specific oven controller instance.
///
/// On first call for a given @p id, resolves all internal TRIAC and sensor handles
/// and sets FlagOvenControllerStatusReady plus the global DeviceStatusFlagsHandle
/// ready bit. Subsequent calls with the same ID return the existing instance
/// without re-opening any device.
OvenControllerRef OCOpen( OvenControllerID id ) {
    if ( id >= OvenControllerCount ) return NULL;
    OvenControllerInstancePtr inst = &instances[ id ];

    if ( !( osEventFlagsGet( inst->statusHandle ) & BIT( FlagOvenControllerStatusReady ) ) ) {
#if FEATURE_HEATER_TOP
        inst->triacTop    = TriacOpen( TriacHeaterTop );
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
        inst->triacRear   = TriacOpen( TriacHeaterRear );
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
        inst->triacBottom = TriacOpen( TriacHeaterBottom );
#endif // FEATURE_HEATER_BOTTOM
#if FEATURE_THERMOCOUPLE_1
        inst->tc1         = TCOpen( Thermocouple1 );
#endif // FEATURE_THERMOCOUPLE_1
#if FEATURE_THERMOCOUPLE_2
        inst->tc2         = TCOpen( Thermocouple2 );
#endif // FEATURE_THERMOCOUPLE_2
#if FEATURE_THERMISTOR_OVEN
        inst->thermistor  = TMOpen( ThermistorOven );
#endif // FEATURE_THERMISTOR_OVEN

        osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusReady ) );
    }

    return inst;
}

/// @brief Start the regulation loop using the supplied parameter block.
///
/// Seeds rampTarget from the average of all available sensors so the ramp begins
/// at the actual cavity temperature rather than zero. Writes statusHandle,
/// currentTemp, and OvenStateHeating back into @p pb, then sets
/// FlagOvenControllerStatusActive to allow OCProcess() to run. The caller must
/// not free or invalidate @p pb while the controller is active.
void OCStart( OvenControllerRef controller, OvenControlPBPtr pb ) {
    if ( controller == NULL || pb == NULL ) return;
    OvenControllerInstancePtr inst = (OvenControllerInstancePtr)controller;

    // Seed rampTarget from all available sensors so the ramp starts from actual cavity temperature
    int32_t sum   = 0;
    uint8_t count = 0;
#if FEATURE_THERMOCOUPLE_1
    if ( inst->tc1        ) { sum += TCGetTemperature( inst->tc1 );        count++; }
#endif // FEATURE_THERMOCOUPLE_1
#if FEATURE_THERMOCOUPLE_2
    if ( inst->tc2        ) { sum += TCGetTemperature( inst->tc2 );        count++; }
#endif // FEATURE_THERMOCOUPLE_2
#if FEATURE_THERMISTOR_OVEN
    if ( inst->thermistor ) { sum += TMGetTemperature( inst->thermistor ); count++; }
#endif // FEATURE_THERMISTOR_OVEN
    Temperature current = count > 0 ? (Temperature)( sum / count ) : 0;

    taskENTER_CRITICAL();
    inst->pb         = pb;
    inst->rampTarget = current;
    inst->lastTickMs = osKernelGetTickCount();
    taskEXIT_CRITICAL();

    pb->statusHandle = inst->statusHandle;
    pb->currentTemp  = current;
    pb->state        = OvenStateHeating;

    osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusActive ) );
}

/// @brief Stop the regulation loop and de-energise all heating elements.
///
/// Atomically clears the PB pointer so OCProcess() will skip this instance on the
/// next tick. Then unconditionally de-energises all TRIAC channels — regardless of
/// which the mandate permitted — so no element can remain live after a stop.
/// Clears FlagOvenControllerStatusActive, FlagOvenControllerStatusAtTemp, and
/// FlagOvenControllerStatusOverTemp. Sets OvenStateIdle in the PB if it is still valid.
void OCStop( OvenControllerRef controller ) {
    if ( controller == NULL ) return;
    OvenControllerInstancePtr inst = (OvenControllerInstancePtr)controller;

    OvenControlPBPtr pb;
    taskENTER_CRITICAL();
    pb       = inst->pb;
    inst->pb = NULL;
    taskEXIT_CRITICAL();

    // De-energise all heaters regardless of which the mandate permitted
#if FEATURE_HEATER_TOP
    if ( inst->triacTop    ) TriacOff( inst->triacTop    );
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
    if ( inst->triacRear   ) TriacOff( inst->triacRear   );
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
    if ( inst->triacBottom ) TriacOff( inst->triacBottom );
#endif // FEATURE_HEATER_BOTTOM

    osEventFlagsClear( inst->statusHandle,
                       BIT( FlagOvenControllerStatusActive  ) |
                       BIT( FlagOvenControllerStatusAtTemp  ) |
                       BIT( FlagOvenControllerStatusOverTemp) );

    if ( pb != NULL ) {
        pb->state = OvenStateIdle;
    }
}

/// @brief Return the full status bitmask for this controller instance.
uint32_t OCGetStatus( OvenControllerRef controller ) {
    if ( controller == NULL ) return BIT( FlagOvenControllerStatusFault );
    return osEventFlagsGet( ( (OvenControllerInstancePtr)controller )->statusHandle );
}

/// @brief Run the regulation loop — read sensors, compute output, drive actuators.
///
/// Called each scheduler tick for all active instances. For each:
///
/// 1. Skip if FlagOvenControllerStatusActive is clear.
/// 2. Grab the PB pointer atomically; skip if NULL.
/// 3. Advance rampTarget toward pb->targetTemp at most pb->rampRate milli-degrees/second.
/// 4. Sample temperature; if over-temp threshold, de-energise and latch fault.
/// 5. Check TRIAC fault; if present, de-energise and latch fault.
/// 6. Clear any prior fault on a clean tick.
/// 7. Apply control law:
///    - Above target by more than tolerance → TriacOff (cooling, wait).
///    - Below rampTarget → TriacOn full power (tracking the ramp).
///    - Within ramp, below target → proportional burst-fire: power scales
///      linearly from 1000 permille at one tolerance below target to 0 at target.
/// 8. Write currentTemp and state back into the PB.
///
/// @warning Do not call from ISR context.
void OCProcess( void ) {
    for ( uint8_t i = 0; i < OvenControllerCount; i++ ) {
        OvenControllerInstancePtr inst = &instances[ i ];

        uint32_t flags = osEventFlagsGet( inst->statusHandle );
        if ( !( flags & BIT( FlagOvenControllerStatusActive ) ) ) continue;

        OvenControlPBPtr pb;
        taskENTER_CRITICAL();
        pb = inst->pb;
        taskEXIT_CRITICAL();
        if ( pb == NULL ) continue;

        // Advance the ramp-limited setpoint toward the final target
        uint32_t    now  = osKernelGetTickCount();
        uint32_t    dtMs = now - inst->lastTickMs;
        inst->lastTickMs = now;

        if ( pb->rampRate == 0 || inst->rampTarget >= pb->targetTemp ) {
            inst->rampTarget = pb->targetTemp;
        } else {
            Temperature step      = (Temperature)( (int64_t)pb->rampRate * dtMs / 1000 );
            Temperature remaining = pb->targetTemp - inst->rampTarget;
            inst->rampTarget     += ( step < remaining ) ? step : remaining;
        }

        Temperature currentTemp = SampleTemperature( inst );

        // Over-temperature — de-energise immediately and latch fault
        if ( currentTemp >= kOverTempThreshold ) {
            DriveHeaters( inst, pb, 0 );
            pb->currentTemp = currentTemp;
            pb->state       = OvenStateCooling;
            osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusOverTemp ) |
                                                 BIT( FlagOvenControllerStatusFault    ) );
            osEventFlagsSet( FaultFlagsHandle,   BIT( kFaultBit[ i ] ) );
            continue;
        }

        if ( HeaterFaulted( inst, pb ) ) {
            DriveHeaters( inst, pb, 0 );
            pb->currentTemp = currentTemp;
            osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusFault ) );
            osEventFlagsSet( FaultFlagsHandle,   BIT( kFaultBit[ i ] ) );
            continue;
        }

        osEventFlagsClear( inst->statusHandle, BIT( FlagOvenControllerStatusFault ) );
        osEventFlagsClear( FaultFlagsHandle,   BIT( kFaultBit[ i ] ) );

        OvenState state;

        if ( pb->targetTemp < currentTemp - pb->tolerance ) {
            // Above target by more than tolerance — wait to cool
            DriveHeaters( inst, pb, 0 );
            state = OvenStateCooling;
            osEventFlagsClear( inst->statusHandle, BIT( FlagOvenControllerStatusAtTemp ) );
        } else if ( currentTemp < inst->rampTarget ) {
            // Below rate-limited setpoint — full power to track the ramp
            DriveHeaters( inst, pb, 1000 );
            state = OvenStateHeating;
            osEventFlagsClear( inst->statusHandle, BIT( FlagOvenControllerStatusAtTemp ) );
        } else {
            // Ramp complete — proportional burst-fire scales linearly from full power at
            // one tolerance below target down to zero at target, minimising overshoot
            Temperature error = pb->targetTemp - currentTemp;
            Permille    power = ( error > 0 && pb->tolerance > 0 )
                                ? (Permille)( (int32_t)1000 * error / pb->tolerance )
                                : 0;
            DriveHeaters( inst, pb, power );
            if ( error <= pb->tolerance ) {
                state = OvenStateAtTemp;
                osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusAtTemp ) );
            } else {
                state = OvenStateHeating;
                osEventFlagsClear( inst->statusHandle, BIT( FlagOvenControllerStatusAtTemp ) );
            }
        }

        pb->currentTemp = currentTemp;
        pb->state       = state;
    }
}

// ============================================================================
// Internal
// ============================================================================

/// @brief Average temperature from all fitted, non-faulted sensor sources.
///
/// Reads each source enabled by Features.h that reports no hardware fault.
/// Returns the integer average of all valid readings. If no source is available,
/// sets FlagOvenControllerStatusFault on @p inst and raises FlagOvenControllerFault
/// in FaultFlagsHandle, then returns 0.
///
/// @param[in]  inst  Controller instance whose device handles are used.
/// @return Averaged temperature in milli-degrees C, or 0 if all sources faulted.
static Temperature SampleTemperature( OvenControllerInstancePtr inst ) {
    static const uint32_t kTCFaultMask = BIT( FlagTCStatusOpenCircuit  ) |
                                          BIT( FlagTCStatusShortToGND   ) |
                                          BIT( FlagTCStatusShortToVCC   ) |
                                          BIT( FlagTCStatusHardwareFault );
    static const uint32_t kTMFaultMask = BIT( FlagTMStatusHardwareFault ) |
                                          BIT( FlagTMStatusOpenCircuit   ) |
                                          BIT( FlagTMStatusShortCircuit  );

    int32_t sum   = 0;
    uint8_t count = 0;

#if FEATURE_THERMOCOUPLE_1
    if ( inst->tc1 && !( TCGetStatus( inst->tc1 ) & kTCFaultMask ) ) {
        sum += TCGetTemperature( inst->tc1 );
        count++;
    }
#endif // FEATURE_THERMOCOUPLE_1
#if FEATURE_THERMOCOUPLE_2
    if ( inst->tc2 && !( TCGetStatus( inst->tc2 ) & kTCFaultMask ) ) {
        sum += TCGetTemperature( inst->tc2 );
        count++;
    }
#endif // FEATURE_THERMOCOUPLE_2
#if FEATURE_THERMISTOR_OVEN
    if ( inst->thermistor && !( TMGetStatus( inst->thermistor ) & kTMFaultMask ) ) {
        sum += TMGetTemperature( inst->thermistor );
        count++;
    }
#endif // FEATURE_THERMISTOR_OVEN

    if ( count == 0 ) {
        osEventFlagsSet( inst->statusHandle, BIT( FlagOvenControllerStatusFault ) );
        osEventFlagsSet( FaultFlagsHandle,   BIT( kFaultBit[ inst->id ] ) );
        return 0;
    }

    return (Temperature)( sum / count );
}

/// @brief Drive all enabled heaters at the requested power level.
///
/// Selects the drive mode based on @p power:
/// - 0        → TriacOff() (gate permanently off)
/// - 1000     → TriacOn()  (gate permanently on)
/// - 1…999   → TriacRun() with zero-cross burst-fire; burstOn is rounded to the
///              nearest half-cycle and clamped to at least 1 to prevent silent off.
///
/// Only channels permitted by the mandate (pb->heaterTop, pb->heaterRear,
/// pb->heaterBottom) with a valid instance handle are driven; unpermitted or
/// NULL channels are silently skipped.
///
/// @param[in] inst   Controller instance whose TRIAC handles are used.
/// @param[in] pb     Parameter block providing mandate heater-enable bits.
/// @param[in] power  Output power in permille (0 = off, 1000 = full).
static void DriveHeaters( OvenControllerInstancePtr inst, OvenControlPBPtr pb, Permille power ) {
    void (*drive)( TriacRef ) = NULL;
    TriacDriveParams burst;

    if ( power == 0 ) {
        drive = TriacOff;
    } else if ( power >= 1000 ) {
        drive = TriacOn;
    } else {
        uint8_t burstOn = (uint8_t)( ( power * kBurstWindow + 500U ) / 1000U );
        if ( burstOn == 0 ) burstOn = 1;
        burst.phaseDelayUs = 0;
        burst.burstOn      = burstOn;
        burst.burstWindow  = kBurstWindow;
    }

#if FEATURE_HEATER_TOP
    if ( pb->heaterTop    && inst->triacTop    ) { if ( drive ) drive( inst->triacTop    ); else TriacRun( inst->triacTop,    burst ); }
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
    if ( pb->heaterRear   && inst->triacRear   ) { if ( drive ) drive( inst->triacRear   ); else TriacRun( inst->triacRear,   burst ); }
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
    if ( pb->heaterBottom && inst->triacBottom ) { if ( drive ) drive( inst->triacBottom ); else TriacRun( inst->triacBottom, burst ); }
#endif // FEATURE_HEATER_BOTTOM
}

/// @brief Return true if any enabled heater TRIAC has lost AC sync or reported a config error.
///
/// Checks FlagTriacStatusZCDLost and FlagTriacStatusConfigError for each channel
/// permitted by the mandate. A NULL instance handle for a permitted channel is not
/// treated as a fault here — OCOpen() guarantees all handles are valid, so NULL
/// would only occur if the channel was never opened (which the mandate bits would
/// prevent from mattering). Returns false immediately if all permitted channels
/// are clean.
///
/// @param[in] inst  Controller instance whose TRIAC handles are used.
/// @param[in] pb    Parameter block providing mandate heater-enable bits.
/// @return true if at least one permitted, open TRIAC channel is reporting a fault.
static bool HeaterFaulted( OvenControllerInstancePtr inst, OvenControlPBPtr pb ) {
    static const uint32_t kFaultMask = BIT( FlagTriacStatusZCDLost    ) |
                                        BIT( FlagTriacStatusConfigError );

#if FEATURE_HEATER_TOP
    if ( pb->heaterTop    && inst->triacTop    && ( TriacGetStatus( inst->triacTop    ) & kFaultMask ) ) return true;
#endif // FEATURE_HEATER_TOP
#if FEATURE_HEATER_REAR
    if ( pb->heaterRear   && inst->triacRear   && ( TriacGetStatus( inst->triacRear   ) & kFaultMask ) ) return true;
#endif // FEATURE_HEATER_REAR
#if FEATURE_HEATER_BOTTOM
    if ( pb->heaterBottom && inst->triacBottom && ( TriacGetStatus( inst->triacBottom ) & kFaultMask ) ) return true;
#endif // FEATURE_HEATER_BOTTOM
    return false;
}
