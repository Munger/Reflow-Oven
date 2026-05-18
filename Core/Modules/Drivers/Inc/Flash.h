/// @file Flash.h
///
/// @brief External NOR flash driver public interface — MX25L51245GZ2I-10G.
///
/// Provides byte-addressed read, page-program, sector-erase, and chip-erase
/// operations over an SPI bus acquired at FlashOpen(). All operations are
/// synchronous and block the calling task until hardware completion.
///
/// Geometry constants are exposed so that FileManager can configure LittleFS
/// partition descriptors without hard-coding chip-specific values.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FLASH_H
#define FLASH_H

#include "Features.h"

#if FEATURE_FLASH

#include <stdbool.h>
#include <stdint.h>

#include "SPIManager.h"

/// @brief Logical identifiers for NOR flash devices managed by this driver.
typedef enum {
    Flash1 = 0,     ///< Primary NOR flash — MX25L51245GZ2I-10G on SPI1
    FlashCount
} FlashID;

/// @brief Diagnostic flag bit positions for the flash driver status event group.
/// Returned by FlashGetStatus(). Maps 1:1 to bits in the private per-instance event flags.
typedef enum {
    FlagFlashStatusReady      = 0,  ///< JEDEC verified and 4-byte mode entered.
    FlagFlashStatusBusy       = 1,  ///< Device WIP bit is currently set.
    FlagFlashStatusJEDECError = 2,  ///< JEDEC ID did not match MX25L51245G.
    FlagFlashStatusSPIError   = 3,  ///< SPI transfer failure.
    FlagFlashStatusTimeout    = 4,  ///< WIP poll timed out.

    FlashStatusFlagsCount           ///< Must stay <= 24.
} FlashStatusBit;

_Static_assert( FlashStatusFlagsCount <= 24, "FlashStatusFlags out of bounds" );

/// @brief Opaque handle to a flash device instance.
typedef struct FlashInstance* FlashRef;

/// @brief Open a handle to a flash device and perform one-time hardware initialisation.
///
/// Verifies the JEDEC ID, enters 4-byte address mode, and sets the Ready flag.
/// On first call for a given ID the device is configured; subsequent calls with
/// the same ID return the existing instance without re-initialising.
///
/// Sets FlagFlashReady in DeviceStatusFlagsHandle on success, or FlagFlashFault
/// in FaultFlagsHandle on JEDEC mismatch or SPI error.
///
/// @param[in] id   Flash device identifier.
/// @param[in] spi  SPI bus handle returned by SPIOpen().
/// @return Handle to the instance; always non-NULL (faults are in status flags).
FlashRef FlashOpen( FlashID id, SPIRef spi );

/// @brief Return a handle to a previously opened flash device without re-initialising.
/// @param[in] id Flash device identifier.
/// @return Handle, or NULL if @p id has not been opened yet.
FlashRef FlashGetRef( FlashID id );

/// @brief Read @p len bytes from byte address @p addr into @p buf.
///
/// No alignment restriction on @p addr or @p len.
///
/// @param[in]  flash Handle returned by FlashOpen().
/// @param[in]  addr  Absolute byte address (0 to NOR_TOTAL_SIZE - 1).
/// @param[out] buf   Destination buffer; must be at least @p len bytes.
/// @param[in]  len   Number of bytes to read.
/// @return true on success, false on SPI error.
bool FlashRead( FlashRef flash, uint32_t addr, void* buf, uint32_t len );

/// @brief Program up to NOR_PAGE_SIZE bytes starting at @p addr.
///
/// @p addr must be page-aligned (multiple of NOR_PAGE_SIZE) and @p len must
/// not exceed NOR_PAGE_SIZE. The sector containing @p addr must be erased before
/// calling. Blocks until the hardware write cycle completes.
///
/// @param[in] flash Handle returned by FlashOpen().
/// @param[in] addr  Page-aligned byte address.
/// @param[in] buf   Source buffer; must be at least @p len bytes.
/// @param[in] len   Number of bytes to program (1 to NOR_PAGE_SIZE).
/// @return true on success, false on SPI error or program timeout.
bool FlashProgram( FlashRef flash, uint32_t addr, const void* buf, uint32_t len );

/// @brief Erase the 4 KB sector that contains @p addr.
///
/// @p addr must be sector-aligned (multiple of NOR_SECTOR_SIZE). Blocks until
/// erase completes (up to ~400 ms).
///
/// @param[in] flash Handle returned by FlashOpen().
/// @param[in] addr  Sector-aligned byte address.
/// @return true on success, false on SPI error or erase timeout.
bool FlashEraseSector( FlashRef flash, uint32_t addr );

/// @brief Erase the entire chip — all bytes set to 0xFF.
///
/// @warning Destructive and slow (up to ~400 s for MX25L51245G). Only call
///          from a partition format operation. Blocks the calling task.
///
/// @param[in] flash Handle returned by FlashOpen().
/// @return true on success, false on SPI error or erase timeout.
bool FlashEraseChip( FlashRef flash );

/// @brief Confirm that no write or erase operation is in progress.
///
/// Used as the LittleFS @c sync callback.
///
/// @param[in] flash Handle returned by FlashOpen().
/// @return true if the device is idle, false on SPI error or timeout.
bool FlashSync( FlashRef flash );

/// @brief Return the page size in bytes for this flash device.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Page size in bytes; 0 if @p flash is NULL.
uint32_t FlashGetPageSize( FlashRef flash );

/// @brief Return the sector size in bytes for this flash device.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Sector size in bytes; 0 if @p flash is NULL.
uint32_t FlashGetSectorSize( FlashRef flash );

/// @brief Return the total capacity in bytes for this flash device.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Total size in bytes; 0 if @p flash is NULL.
uint32_t FlashGetTotalSize( FlashRef flash );

/// @brief Return the number of erasable sectors for this flash device.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Sector count; 0 if @p flash is NULL.
uint32_t FlashGetSectorCount( FlashRef flash );

/// @brief Return the raw flash status event flag bits for diagnostics and system snapshots.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Bitmask of FlashStatusBit values; 0 if @p flash is NULL.
uint32_t FlashGetStatus( FlashRef flash );

#endif // FEATURE_FLASH

#endif // FLASH_H
