/// @file USBCDC.h
///
/// @brief USB CDC virtual COM port driver.
///
/// Manages USB CDC ACM instances. USBCDCProcess() drives the transmit pipeline
/// (dequeuing serialised APIBuffer chains from the output queue and submitting
/// them via CDC_Transmit_FS()). USBTxDoneHandler() is called from the USB ISR
/// on TX completion and advances or closes the chain.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef USBCDC_H
#define USBCDC_H

#include "Features.h"
#include "Types.h"
#include "SystemStatusFlags.h"

/// @brief Logical identifiers for USB CDC instances managed by this driver.
typedef enum {
    USBCDC0 = 0,   ///< Primary USB CDC ACM port
    USBCDC1,       ///< Secondary USB CDC ACM port (reserved for future use)
    USBCDCInstanceCount
} USBCDCID;

/// @brief Status and diagnostic flag bit positions for a CDC instance.
/// These map 1:1 to the bits in the per-instance statusHandle event flag group.
typedef enum {
    FlagUSBCDCStatusReady = 0,   ///< Instance initialised and the transmit pipeline is operational

    USBCDCStatusFlagsCount
} USBCDCStatusBit;

_Static_assert( USBCDCStatusFlagsCount <= 24, "USBCDCStatusFlags out of bounds" );

/// @brief Opaque handle to a USB CDC instance.
typedef struct USBCDCInstance* USBCDCRef;

/// @brief Allocate per-instance resources and initialise the API core and stream parser.
void       USBCDCInitModule( void );

/// @brief Open a handle to a specific CDC instance.
///
/// Idempotent — subsequent calls with the same @p id return the existing handle.
///
/// @param[in] id  CDC instance identifier.
/// @return Handle to the instance; NULL if @p id is out of range.
USBCDCRef  USBCDCOpen( USBCDCID id );

/// @brief Return the full status bitmask for a CDC instance.
/// @param[in] cdc  Handle returned by USBCDCOpen().
/// @return Bitmask of USBCDCStatusBit flags; 0 if @p cdc is NULL.
uint32_t   USBCDCGetStatus( USBCDCRef cdc );

/// @brief Dispatch pending requests and drive the CDC transmit chain.
void       USBCDCProcess( void );

/// @brief CDC TX-complete callback — advance or close the current buffer chain.
/// @warning Called from USB ISR context. FromISR notification variants only.
void       USBTxDoneHandler( void );

#endif // USBCDC_H
