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

#include <stdbool.h>

#include "Types.h"
#include "SystemStatusFlags.h"

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

    REFlagsCount
} REStatusBit;

_Static_assert( REFlagsCount <= 24, "OvenFanStatusFlags out of bounds" );

/// @brief Opaque handle to the singleton rotary encoder instance.
typedef struct RotaryEncoder* RotaryEncoderRef;

/// @brief Initialise the encoder module state and clear the private status flags.
void             REInitModule( void );

/// @brief Return a handle to the singleton encoder instance.
/// @return Always returns a valid non-NULL reference.
RotaryEncoderRef REOpen( void );

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

#endif // ROTARYENCODER_H
