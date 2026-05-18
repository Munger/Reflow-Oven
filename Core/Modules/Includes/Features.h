/// @file Features.h
///
/// @brief Compile-time feature selection for the VesuviOven firmware.
///
/// Edit the PRIMARY CONFIGURATION section only. Set each flag to 1 if the
/// feature is physically fitted or intentionally enabled, 0 otherwise.
/// Derived features below are resolved automatically — do not edit them.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FEATURES_H
#define FEATURES_H

// ============================================================================
// PRIMARY CONFIGURATION — edit this section only
// ============================================================================

// Heating elements
#define FEATURE_HEATER_TOP             1
#define FEATURE_HEATER_REAR            1
#define FEATURE_HEATER_BOTTOM          1

// Temperature sensors
#define FEATURE_THERMOCOUPLE_1         1   ///< MAX31856 channel 1.
#define FEATURE_THERMOCOUPLE_2         1   ///< MAX31856 channel 2.
#define FEATURE_THERMISTOR_CJT_1       1   ///< Cold-junction NTC for thermocouple 1.
#define FEATURE_THERMISTOR_CJT_2       1   ///< Cold-junction NTC for thermocouple 2.
#define FEATURE_THERMISTOR_OVEN        1   ///< Oven cavity NTC (ADC).
#define FEATURE_THERMISTOR_HEATSINK    1   ///< Heatsink NTC — I2C module (EMC2101), independent of FEATURE_THERMISTORS.

// Actuators and indicators
#define FEATURE_OVEN_FAN               1   ///< AC convection fan inside oven cavity.
#define FEATURE_BOARD_FAN              1   ///< DC board cooling fan (EMC2101).
#define FEATURE_OVEN_LIGHT             1   ///< Oven interior light (TRIAC-switched).
#define FEATURE_BUZZER                 1   ///< Piezo buzzer.

// Connectivity and power
#define FEATURE_USB_PD                 1   ///< USB Power Delivery (sink and source).

// MCU peripherals
#define FEATURE_RTC                    1   ///< Real-time clock (MCUGetTime / MCUSetTime).
#define FEATURE_BATTERY_MONITOR        1   ///< Battery voltage and charge-level monitoring via VBAT ADC.
#define FEATURE_MCU_TEMP_MONITOR       1   ///< MCU internal junction temperature via on-chip ADC channel.
#define FEATURE_VCC_MONITOR            1   ///< Supply voltage monitoring via VREFINT ADC channel.
#define FEATURE_HARDWARE_CRC           1   ///< Hardware CRC-32 unit — requires CRC peripheral enabled in CubeMX.

// Storage
#define FEATURE_FLASH                  1   ///< External NOR flash (W25Q512).

// Application
#define FEATURE_LOGGING_FILE           1   ///< Log events to filesystem. Requires FEATURE_FLASH.
#define FEATURE_LOGGING_SERIAL         1   ///< Stream log events over USB CDC.
#define FEATURE_THERMAL_CALIBRATION    1   ///< Thermal calibration — dependencies TBD.

// User interface
#define FEATURE_CONTROL_PANEL          0   ///< Physical control panel. Future enhancement.

// ============================================================================
// DERIVED FEATURES
// ============================================================================

// Thermocouple module: present if at least one channel is fitted.
#if FEATURE_THERMOCOUPLE_1 || FEATURE_THERMOCOUPLE_2
    #define FEATURE_THERMOCOUPLES   1
#else
    #define FEATURE_THERMOCOUPLES   0
#endif

// ADC thermistor module: present if at least one ADC-based NTC is fitted.
// Note: FEATURE_THERMISTOR_HEATSINK is independent — it uses ThermistorI2C.c.
#if FEATURE_THERMISTOR_CJT_1 || FEATURE_THERMISTOR_CJT_2 || FEATURE_THERMISTOR_OVEN
    #define FEATURE_THERMISTORS   1
#else
    #define FEATURE_THERMISTORS   0
#endif

// Rotary encoder: only meaningful with the oven fan.
#if FEATURE_OVEN_FAN
    #define FEATURE_ROTARY_ENCODER   1
#else
    #define FEATURE_ROTARY_ENCODER   0
#endif

// Filesystem: requires flash.
#if FEATURE_FLASH
    #define FEATURE_FILE_SYSTEM   1
#else
    #define FEATURE_FILE_SYSTEM   0
#endif

// File logging: requires the filesystem — silently disabled if flash is absent.
#if FEATURE_LOGGING_FILE && !FEATURE_FILE_SYSTEM
    #undef  FEATURE_LOGGING_FILE
    #define FEATURE_LOGGING_FILE   0
#endif

// Logging infrastructure: present if any logging destination is active.
#if FEATURE_LOGGING_FILE || FEATURE_LOGGING_SERIAL
    #define FEATURE_LOGGING   1
#else
    #define FEATURE_LOGGING   0
#endif

// AC fan calibration: requires the fan and its encoder.
#if FEATURE_OVEN_FAN && FEATURE_ROTARY_ENCODER
    #define FEATURE_AC_FAN_CALIBRATION   1
#else
    #define FEATURE_AC_FAN_CALIBRATION   0
#endif

// Safety constraint: rear heater requires the oven fan for safe operation.
// Running the rear element without convection risks localised overheating.
#if !FEATURE_OVEN_FAN
    #undef  FEATURE_HEATER_REAR
    #define FEATURE_HEATER_REAR   0
#endif

// Safety constraint: all heaters require at least one temperature sensor.
// Without a thermocouple or the oven cavity thermistor there is no closed-loop
// control and no way to detect runaway — disable all heating elements.
#if !FEATURE_THERMOCOUPLES && !FEATURE_THERMISTOR_OVEN
    #undef  FEATURE_HEATER_TOP
    #define FEATURE_HEATER_TOP    0
    #undef  FEATURE_HEATER_REAR
    #define FEATURE_HEATER_REAR   0
    #undef  FEATURE_HEATER_BOTTOM
    #define FEATURE_HEATER_BOTTOM 0
#endif

// Hardware CRC: validate that the CubeMX peripheral is configured if the feature is enabled.
#if FEATURE_HARDWARE_CRC && !__has_include("crc.h")
    #error "FEATURE_HARDWARE_CRC enabled but CRC peripheral not configured in CubeMX"
#endif

#endif // FEATURES_H
