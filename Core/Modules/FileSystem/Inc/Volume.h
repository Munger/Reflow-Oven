/// @file Volume.h
///
/// @brief Volume (mounted filesystem instance) management.
///
/// A volume is a filesystem mounted on a partition. VolMount() instantiates
/// the appropriate filesystem driver for the partition type (currently LittleFS)
/// and brings it online. If no valid filesystem is detected on the partition it
/// is formatted automatically before mounting. VolUnmount() flushes pending
/// writes and tears the instance down cleanly.
///
/// Volume-level mount flags are enforced on top of partition-level flags:
/// a read-only mount flag overrides any file permission that would allow writes.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef VOLUME_H
#define VOLUME_H

#include <stdint.h>
#include "FSTypes.h"
#include "Partition.h"

// ============================================================================
// Limits
// ============================================================================

/// @brief Maximum number of simultaneously mounted volumes.
enum { kVolMaxCount = 4 };

// ============================================================================
// Mount flags and status bits
// ============================================================================

/// @brief Options that modify how a partition is mounted.
typedef enum {
    FSMountDefault  = 0x00, ///< Normal read-write mount
    FSMountReadOnly = 0x01, ///< Force read-only regardless of partition flags
    FSMountNoExec   = 0x02, ///< Disallow execute permission on all files in this volume
} FSMountFlags;

/// @brief Status bit positions for a mounted volume.
typedef enum {
    FlagVolMounted   = 0, ///< Volume is currently mounted
    FlagVolReadOnly  = 1, ///< Volume was mounted or forced read-only
    FlagVolFormatted = 2, ///< Partition was formatted at mount time (no prior filesystem found)
    FlagVolError     = 3, ///< Last operation encountered a filesystem error
    VolStatusFlagsCount
} VolStatusBit;

// ============================================================================
// Opaque handle
// ============================================================================

/// @brief Opaque handle to a mounted volume.
typedef struct FSVolume* VolRef;

// ============================================================================
// Public API
// ============================================================================

/// @brief Mount a partition and return a volume handle.
///
/// Selects the filesystem driver based on the partition type. For LittleFS
/// partitions: attempts lfs_mount(); on failure (no valid filesystem found)
/// runs lfs_format() then lfs_mount(). FlagVolFormatted is set if the
/// partition was formatted.
///
/// Mount flags are unioned with any read-only flag already set in the
/// partition entry, so FSPartFlagReadOnly always wins.
///
/// @param part  Partition handle returned by PartGetByIndex() or PartGetByName().
/// @param flags Mount options.
/// @return Volume handle, or NULL if the pool is full or the mount fails.
VolRef VolMount( PartRef part, FSMountFlags flags );

/// @brief Unmount a volume, flushing all pending writes.
///
/// @param vol Handle returned by VolMount().
/// @return FSResultOk, or FSResultBusy if one or more files are still open.
FSResult VolUnmount( VolRef vol );

/// @brief Erase and reinitialise the filesystem on a mounted volume.
///
/// All data on the partition is permanently destroyed. Requires UID 0
/// (System) — FSResultPermission is returned for any other caller.
///
/// @param vol Handle returned by VolMount().
/// @param uid Caller's UID; must be 0.
/// @return FSResultOk, FSResultPermission, or FSResultIO.
FSResult VolFormat( VolRef vol, FSUid uid );

/// @brief Return the status bitmask for a volume.
/// @param vol Handle returned by VolMount().
/// @return Bitmask of VolStatusBit values, or 0 if @p vol is NULL.
uint32_t VolGetStatus( VolRef vol );

/// @brief Return the partition backing a mounted volume.
/// @param vol Handle returned by VolMount().
/// @return Partition handle, or NULL if @p vol is NULL.
PartRef VolGetPartition( VolRef vol );

#endif // VOLUME_H
