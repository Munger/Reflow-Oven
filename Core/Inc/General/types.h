#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef __weak
#define __weak __attribute__((weak))
#endif

// Temperature in milli-degrees Celsius. 25000 = 25.000 C.
typedef int32_t Temperature;

// Rotational speed in Revolutions Per Minute.
typedef uint16_t Rpm;

// Percentage represented in tenths of a percent (0-1000). 1000 = 100.0%
typedef uint16_t Permille;

// Time duration or offset in milliseconds.
typedef uint32_t DurationMs;

// Raw ADC reading from the STM32G0 12-bit converter.
typedef uint16_t AdcRaw;

// Voltage in millivolts. 3300 = 3.3V.
typedef uint16_t Voltage;

// Current in milliamps. 1000 = 1A.
typedef uint16_t Current;

// Power in milliwatts. 1000 = 1W.
typedef uint32_t Power;

// Motor drive stress factor in centi-units (×100). 0 = no stress, 100000 = STRESS_FACTOR_MAX.
typedef uint32_t StressFactor;

#endif // TYPES_H