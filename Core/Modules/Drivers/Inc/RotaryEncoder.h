/// @file RotaryEncoder.h
///
/// @brief AS5600 magnetic rotary encoder driver for oven fan speed measurement.
///
/// Reads the 12-bit angle register and magnetic status from an AS5600 over I2C.
/// REProcess() drives the async I2C state machine and updates the velocity cache.
/// REGetVelocity() returns a cached value safe to call from any task context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include "Features.h"

#if FEATURE_ROTARY_ENCODER

#include <stdbool.h>

#include "Types.h"
#include "SystemStatusFlags.h"
#include "I2CManager.h"

/// @brief Logical identifiers for rotary encoder instances managed by this driver.
typedef enum {
    RotaryEncoder1 = 0,  ///< AS5600 encoder on the oven fan shaft
    RotaryEncoderCount
} RotaryEncoderID;

/// @brief Status and diagnostic flag bit positions for the rotary encoder / oven fan module.
/// These map 1:1 to the bits in the private ovenFanStatus event flag group.
typedef enum {
    FlagREStatusReady = 0,          ///< Encoder communicating and providing valid data
    FlagREStatusSpinning,            ///< Absolute velocity above RE_SPINNING_THRESHOLD_RPM
    FlagREStatusStall,               ///< Reserved — not currently set by the driver
    FlagREStatusMagWeak,             ///< AS5600 reports magnet too far (AGC high)
    FlagREStatusMagStrong,           ///< AS5600 reports magnet too close (AGC low)
    FlagREStatusMagMissing,          ///< AS5600 MD bit clear — no magnet detected
    FlagREStatusHardwareFault,       ///< I2C communication failure
    FlagREIODone,                    ///< Most recent async I2C read completed successfully.
    FlagREIOError,                   ///< Most recent async I2C read failed.

    REFlagsCount
} REStatusBit;

_Static_assert( REFlagsCount <= 24, "OvenFanStatusFlags out of bounds" );

/// @brief Opaque handle to the singleton rotary encoder instance.
typedef struct RotaryEncoder* RotaryEncoderRef;

/// @brief Allocate per-instance resources and initialise the tick counter.
void             REInitModule( void );

/// @brief Open a handle to a specific encoder instance.
///
/// On first call for a given ID: stores @p i2c in the instance. Subsequent calls
/// with the same ID return the existing instance without re-configuring.
///
/// @param[in] id   Encoder identifier.
/// @param[in] i2c  I2C bus handle returned by I2COpen().
/// @return Handle to the instance, or NULL if @p id is out of range.
RotaryEncoderRef REOpen( RotaryEncoderID id, I2CRef i2c );

/// @brief Return a handle to a previously opened encoder instance without re-initialising.
/// @param[in] id Encoder identifier.
/// @return Handle, or NULL if @p id has not been opened yet or is out of range.
RotaryEncoderRef REGetRef( RotaryEncoderID id );

/// @brief Return the most recently computed rotational velocity.
/// @param[in] encoder Handle returned by REOpen().
/// @return Velocity in RPM (signed; positive = forward). Returns 0 if @p encoder is NULL.
/// @note Returns a cached value computed by REProcess() — safe to call from any task context.
Rpm              REGetVelocity( RotaryEncoderRef encoder );

/// @brief Return the full status bitmask from the private ovenFanStatus flags.
/// @return Bitmask of REStatusBit flags; safe to call from any task context.
uint32_t         REGetStatus( void );

/// @brief Drive the I2C state machine, update velocity, and update status flags.
/// @warning All I2C hardware access occurs here. Do not call from ISR context.
void REProcess( void );

#endif // FEATURE_ROTARY_ENCODER

#endif // ROTARYENCODER_H
