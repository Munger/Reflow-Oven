/// @file USBPDTask.c
///
/// @brief USBPD task — intentional hollow placeholder.
///
/// This task stub is a CubeMX-generated thread kept for future use.
/// USBPD initialisation and processing are handled by ManagerTask and
/// DeviceTask respectively. This task blocks indefinitely and performs no work.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "USBPDTask.h"

/// @brief No-op initialiser for the USBPD placeholder task.
void USBPDTaskInit( void ) {
}

/// @brief USBPD placeholder task loop — blocks indefinitely.
/// @note Intentionally empty. All USBPD work is done in DeviceTaskLoop().
void USBPDTaskLoop( void ) {
    osDelay( portMAX_DELAY );
}
