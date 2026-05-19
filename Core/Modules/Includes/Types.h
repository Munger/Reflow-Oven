/// @file Types.h
///
/// @brief Project-wide primitive type aliases.
///
/// Defines strongly-typed aliases for the fundamental physical and logical
/// quantities used throughout the firmware. Using distinct types rather than
/// raw `uint16_t` / `uint32_t` makes units explicit at function boundaries
/// and reduces the chance of scale-factor mistakes (e.g. passing millidegrees
/// where degrees are expected).
///
/// All types are plain integer aliases — no struct overhead, no hidden indirection.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/// @brief Mark a function as a weak symbol, allowing it to be overridden at link time.
///
/// Used extensively in apihandlers.c so that application code can provide
/// strong overrides without modifying the stub file.
#ifndef __weak
#define __weak __attribute__((weak))
#endif

/// @brief Temperature in milli-degrees Celsius.
///
/// A value of 25000 represents 25.000 °C. Using milli-degrees avoids floating
/// point while retaining three decimal places of precision.
typedef int32_t Temperature;

/// @brief Ramp rate in milli-degrees Celsius per second.
///
/// A value of 2000 represents 2.000 °C/s. Positive = heating, negative = cooling.
/// Stored as milli-°C/s so it has the same scale as Temperature and requires
/// no floating-point arithmetic.
typedef int32_t RampRate;

/// @brief Rotational speed in Revolutions Per Minute.
typedef uint16_t Rpm;

/// @brief Percentage expressed in tenths of a percent (permille), range 0–1000.
///
/// A value of 1000 represents 100.0 %, 500 represents 50.0 %, etc.
typedef uint16_t Permille;

/// @brief Time duration or offset in milliseconds.
typedef uint32_t DurationMs;

/// @brief Raw ADC reading from the STM32G0 12-bit converter, range 0–4095.
typedef uint16_t AdcRaw;

/// @brief Voltage in millivolts.
///
/// A value of 3300 represents 3.3 V.
typedef uint16_t Voltage;

/// @brief Current in milliamps.
///
/// A value of 1000 represents 1 A.
typedef uint16_t Current;

/// @brief Power in milliwatts.
///
/// A value of 1000 represents 1 W.
typedef uint32_t Power;

/// @brief Motor drive stress factor in centi-units (×100).
///
/// Range: 0 (no stress) to `AC_FAN_STRESS_FACTOR_MAX` (100,000,000).
/// Used by the AC fan tuning engine to rank candidate drive parameters.
typedef uint32_t StressFactor;

/// @brief Single linearisation table entry for an NTC thermistor (milli-degrees C).
typedef Temperature NTCEntry;

/// @brief Pointer to the first entry of an NTC thermistor lookup table.
/// Tables have 33 entries covering 32 equal intervals of 128 ADC counts each
/// (12-bit ADC range: 4096 / 32 = 128 counts per step).
typedef const NTCEntry* NTCEntryPtr;

#endif // TYPES_H
