/// @file Firmware.h
///
/// @brief Product identity and firmware version constants.
///
/// Defines the authoritative product name, manufacturer, hardware revision,
/// and firmware version for the build. Referenced by USB descriptors and
/// any other subsystem that needs product identity at compile time.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FIRMWARE_H
#define FIRMWARE_H

// ============================================================================
// PRODUCT IDENTITY
// ============================================================================

#define FIRMWARE_MANUFACTURER     "MungerWare"
#define FIRMWARE_PRODUCT          "VesuviOven MagmaFlow V1"
#define FIRMWARE_HW_REVISION      1

// ============================================================================
// USB DESCRIPTORS
// ============================================================================

#define FIRMWARE_USB_VID          1155    ///< STMicroelectronics VID (default).
#define FIRMWARE_USB_PID          22336   ///< CubeMX-assigned PID.
#define FIRMWARE_USB_LANGID       1033    ///< English (United States).

// ============================================================================
// FIRMWARE VERSION
// ============================================================================

#define FIRMWARE_VERSION_MAJOR    0
#define FIRMWARE_VERSION_MINOR    1
#define FIRMWARE_VERSION_PATCH    0

#define FIRMWARE_VERSION_STRING   "0.1.0"

#endif // FIRMWARE_H
