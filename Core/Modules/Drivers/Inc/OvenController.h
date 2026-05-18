/// @file OvenController.h
///
/// @brief Oven temperature regulator.
///
/// OvenController is a pure regulator — it knows nothing about reflow profiles
/// or drying schedules. The Reflow task fills an OvenControlPB block with the
/// target temperature, tolerance, ramp rate, and resource bits, then calls
/// OCStart(). The controller drives the permitted TRIAC channels to achieve and
/// hold the target, updating currentTemp and state in the PB each tick. The
/// Reflow task may update any mandate field directly at any time; the controller
/// picks up changes on the next OCProcess() tick.
///
/// TRIAC channels and temperature sensors are resolved internally — they are
/// fixed on the board and not configurable at runtime.
///
/// TC1 is wired to an FR4 reference piece at board level (board-surface
/// temperature); TC2 protrudes from the same piece into free air. These
/// positions are fixed by wiring convention.
///
/// The oven fan is not managed here; the profile drives ACFan directly.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef OVENCONTROLLER_H
#define OVENCONTROLLER_H

#include <stdint.h>

#include "Features.h"
#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Logical identifiers for oven controller instances.
typedef enum {
    OvenController1 = 0,  ///< Primary oven controller instance
    OvenControllerCount
} OvenControllerID;

/// @brief Regulation state reported by the controller in OvenControlPB.state.
typedef enum {
    OvenStateIdle = 0,  ///< Controller is not running
    OvenStateHeating,    ///< Driving heaters toward target
    OvenStateCooling,    ///< Target is below current temperature; waiting to cool
    OvenStateAtTemp,     ///< Measured temperature within tolerance of target

    OvenStateCount
} OvenState;

/// @brief Status and diagnostic flag bit positions for the oven controller instance.
typedef enum {
    FlagOvenControllerStatusReady = 0,   ///< Internal devices resolved; regulation may run
    FlagOvenControllerStatusActive,       ///< Regulation loop is running
    FlagOvenControllerStatusAtTemp,       ///< Measured temperature within tolerance of target
    FlagOvenControllerStatusOverTemp,     ///< Measured temperature exceeded safe maximum
    FlagOvenControllerStatusFault,        ///< Required sensor or actuator has faulted

    OvenControllerFlagsCount
} OvenControllerStatusBit;

_Static_assert( OvenControllerFlagsCount <= 24, "OvenControllerStatusFlags out of bounds" );

/// @brief Opaque handle to an oven controller instance.
typedef struct OvenControllerInstance* OvenControllerRef;

/// @brief Shared control and status block for an oven regulation run.
///
/// The Reflow task owns this block and may update mandate fields at any time.
/// The controller holds a pointer to it for the duration of the run, reading
/// mandate fields and writing currentTemp and state each tick. Fields written
/// by the controller should be treated as read-only by the Reflow task.
///
/// TC1 measures board-surface temperature (embedded in FR4 reference piece).
/// TC2 measures free-air temperature at the same height (protruding from the same piece).
typedef struct {
    // Mandate — written by Reflow task, read by controller each tick
    Temperature targetTemp;   ///< Desired cavity temperature in milli-degrees C
    Temperature tolerance;    ///< Acceptable deviation from target in milli-degrees C
    Temperature rampRate;     ///< Maximum rate of rise in milli-degrees C per second; 0 = unlimited
    union {
        struct {
            uint8_t heaterTop    : 1;  ///< Permit the top heater.
            uint8_t heaterRear   : 1;  ///< Permit the rear convection element.
            uint8_t heaterBottom : 1;  ///< Permit the bottom heater.
            uint8_t              : 5;  ///< Reserved — must be zero.
        };
        uint8_t heaters;               ///< All heater bits as a byte; matches ReflowStage.heaters.
                                       ///<  Bit positions are fixed. OvenController.c gates actual
                                       ///<  drive output against Features.h at compile time.
    };

    // Observable — written by OCStart(), read by Reflow task
    osEventFlagsId_t statusHandle;  ///< Controller status flags; wait on BIT(FlagOvenControllerStatusAtTemp)
                                    ///  to block until at temperature

    // Observable — written by OCProcess(), read by Reflow task
    Temperature currentTemp;  ///< Averaged cavity temperature from active sources, in milli-degrees C
    OvenState   state;        ///< Current regulation state
} OvenControlPB, *OvenControlPBPtr;

/// @brief Allocate per-instance resources. Does not access hardware.
void              OCInitModule( void );

/// @brief Open a handle to a specific oven controller instance.
///
/// Resolves all internal TRIAC and sensor handles. Sets FlagOvenControllerStatusReady
/// and signals DeviceStatusFlagsHandle on success.
///
/// @param[in] id Controller instance identifier.
/// @return Handle to the instance, or NULL if @p id is out of range.
OvenControllerRef OCOpen( OvenControllerID id );

/// @brief Start the regulation loop using the supplied parameter block.
///
/// The controller holds a pointer to @p pb for the duration of the run,
/// reading mandate fields and writing currentTemp and state each tick.
/// The caller must not free or invalidate @p pb while the controller is running.
///
/// @param[in] controller Handle returned by OCOpen().
/// @param[in] pb         Parameter block owned by the caller.
void              OCStart( OvenControllerRef controller, OvenControlPBPtr pb );

/// @brief Stop the regulation loop and de-energise all heating elements.
/// @param[in] controller Handle returned by OCOpen().
void              OCStop( OvenControllerRef controller );

/// @brief Return the full status bitmask for this controller instance.
/// @param[in] controller Handle returned by OCOpen().
/// @return Bitmask of OvenControllerStatusBit flags; BIT(FlagOvenControllerStatusFault) if NULL.
uint32_t          OCGetStatus( OvenControllerRef controller );

/// @brief Run the regulation loop — read sensors, compute output, drive actuators.
/// @warning Do not call from ISR context.
void              OCProcess( void );

#endif // OVENCONTROLLER_H
