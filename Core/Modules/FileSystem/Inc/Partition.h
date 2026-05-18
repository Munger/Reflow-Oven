/// @file Partition.h
///
/// @brief Partition table management for the FileSystem subsystem.
///
/// The partition table is stored in block 0 of the block device and read at
/// PartInitModule() time. If no valid table is found on the device, the
/// compiled-in default layout is written and then used. Changes to the
/// in-memory table are persisted by calling PartCommit().
///
/// Each partition entry records:
///   - A human-readable name (up to kPartNameLen − 1 chars)
///   - Start block and block count within the device
///   - Filesystem type (LittleFS, Raw, Reserved)
///   - Mount flags (read-only, USB-visible, hidden)
///   - Owner UID and Unix-style permission mode
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef PARTITION_H
#define PARTITION_H

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <stdint.h>
#include <stdbool.h>
#include "FSTypes.h"
#include "BlockDevice.h"

// ============================================================================
// Limits and magic
// ============================================================================

/// @brief Maximum number of partition entries in the table.
enum { kPartMaxCount = 8 };

/// @brief Well-known partition names.
#define PART_NAME_SYSTEM    "System"   ///< Read-only system partition — firmware assets and config.
#define PART_NAME_USER      "User"     ///< Read-write user partition — profiles, logs, calibration.

/// @brief Maximum partition name length including the NUL terminator.
enum { kPartNameLen = 16 };

/// @brief Magic word stored at the start of a valid on-disk partition table.
///        ASCII "FSPT" in little-endian order.
#define PART_TABLE_MAGIC 0x54505346UL

/// @brief Partition table format version written to on-chip storage.
enum { kPartTableVersion = 1 };

// ============================================================================
// Partition type and flags
// ============================================================================

/// @brief Filesystem type for a partition entry.
typedef enum {
    FSPartLittleFS = 0, ///< LittleFS filesystem
    FSPartRaw      = 1, ///< Raw byte-addressable storage (no filesystem layer)
    FSPartReserved = 2, ///< Reserved block — not to be mounted or exposed
} FSPartType;

/// @brief Partition flag bits. Combine with bitwise OR.
typedef enum {
    FSPartFlagReadOnly   = 0x01, ///< Volume always mounts read-only
    FSPartFlagUsbVisible = 0x02, ///< Exposed to USB mass storage host
    FSPartFlagHidden     = 0x04, ///< Not enumerated to User (non-System) contexts
} FSPartFlags;

// ============================================================================
// Partition entry (in-memory and on-disk representation)
// ============================================================================

/// @brief A single partition table entry.
///
/// The on-disk layout matches this struct exactly (no implicit padding —
/// fields are ordered and sized to be naturally aligned on 32-bit targets).
typedef struct FSPartEntry {
    char     name[ kPartNameLen ]; ///< Null-terminated partition name
    uint32_t startBlock;           ///< Index of the first block of this partition
    uint32_t blockCount;           ///< Number of blocks in this partition
    uint8_t  type;                 ///< FSPartType
    uint8_t  flags;                ///< FSPartFlags bitmask
    FSUid    uid;                  ///< Owner UID
    uint8_t  reserved;             ///< Explicit padding — must be 0
    FSMode   mode;                 ///< Unix-style permission mode
    uint8_t  pad[ 2 ];             ///< Align struct to 32 bytes
} FSPartEntry, *FSPartEntryPtr;

// ============================================================================
// Opaque partition handle
// ============================================================================

/// @brief Opaque handle to a partition table entry.
typedef struct FSPartition* PartRef;

// ============================================================================
// Public API
// ============================================================================

/// @brief Initialise the partition subsystem against a block device.
///
/// Reads block 0 of @p bd. If a valid partition table (correct magic,
/// version, and checksum) is found it is loaded into the in-memory table.
/// Otherwise the compiled-in default layout is written to block 0 and used.
/// Must be called before any other Partition function.
///
/// @param bd Block device handle returned by BDRegister().
/// @return FSResultOk on success, or FSResultIO if block 0 is unreadable.
FSResult PartInitModule( BDRef bd );

/// @brief Return the number of valid partition entries currently loaded.
uint8_t PartGetCount( void );

/// @brief Return a handle to the partition at the given zero-based index.
/// @return Handle, or NULL if @p index is out of range.
PartRef PartGetByIndex( uint8_t index );

/// @brief Return a handle to the first partition with the given name.
/// @return Handle, or NULL if no partition with that name exists.
PartRef PartGetByName( const char* name );

/// @brief Copy the entry descriptor for a partition into @p out.
/// @param part Handle returned by PartGetByIndex() or PartGetByName().
/// @param out  Struct to populate.
/// @return FSResultOk, or FSResultInvalid if @p part or @p out is NULL.
FSResult PartGetEntry( PartRef part, FSPartEntryPtr out );

/// @brief Return the block device on which this partition resides.
/// @param part Handle returned by PartGetByIndex() or PartGetByName().
/// @return BDRef, or NULL if @p part is NULL.
BDRef PartGetDevice( PartRef part );

/// @brief Write the in-memory partition table back to block 0 of the device.
///
/// Erases block 0, then programs the full table record (header + entries +
/// checksum). Should be called after any modification to the partition layout.
///
/// @return FSResultOk on success, or FSResultIO on erase/program failure.
FSResult PartCommit( void );

#endif // FEATURE_FILE_SYSTEM

#endif // PARTITION_H
