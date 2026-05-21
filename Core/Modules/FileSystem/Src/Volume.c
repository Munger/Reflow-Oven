/// @file Volume.c
///
/// @brief Volume (mounted filesystem instance) management.
///
/// Maintains a fixed-size pool of FSVolume instances. Each volume holds a
/// live lfs_t, an lfs_config, and the static cache buffers LittleFS requires.
/// LFSAdapter.c provides the block device callbacks; this file wires everything
/// together at mount time.
///
/// The open-file reference count is maintained here (incremented/decremented by
/// FSFile.c via the FSInternal.h accessors) so that VolUnmount() can refuse to
/// tear down a volume that still has open handles.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <string.h>
#include "Volume.h"
#include "FSInternal.h"
#include "SafeStdLib.h"
#include "lfs.h"

// ============================================================================
// Cache dimensions (must match LFSAdapter.c constants)
// ============================================================================

enum { kLfsCacheSize = 256 };
enum { kLfsLookahead = 16  };

// ============================================================================
// Forward declaration (LFSAdapter.c)
// ============================================================================

void LFSAdapterConfigure( struct lfs_config* cfg,
                           VolRef             vol,
                           uint32_t           blockCount,
                           uint8_t*           readBuf,
                           uint8_t*           progBuf,
                           uint8_t*           lookahead );

// ============================================================================
// Private instance type
// ============================================================================

typedef struct FSVolume {
    PartRef           partition;
    FSMountFlags      mountFlags;
    uint32_t          statusBits;
    uint8_t           openFileCount;
    char              mountPoint[ kPartNameLen + 1 ]; ///< e.g. "/system"
    lfs_t             lfs;
    struct lfs_config lfsCfg;
    uint8_t           readBuf[ kLfsCacheSize ];
    uint8_t           progBuf[ kLfsCacheSize ];
    uint8_t           lookaheadBuf[ kLfsLookahead ];
} FSVolume, *FSVolumePtr;

#define BIT_SHIFT( n ) ( 1UL << (uint32_t)( n ) )

static FSVolume pool[ kVolMaxCount ];

// ============================================================================
// Package-private accessors (declared in FSInternal.h)
// ============================================================================

/// @brief Return the LittleFS instance for a mounted volume.
/// @param[in] vol  Volume handle, or NULL.
/// @return Pointer to lfs_t, or NULL if vol is NULL or unmounted.
lfs_t* VolGetLFS( VolRef vol ) {
    if ( vol == NULL ) return NULL;
    if ( !( vol->statusBits & BIT_SHIFT( FlagVolMounted ) ) ) return NULL;
    return &vol->lfs;
}

/// @brief Return the number of open files on a volume.
/// @param[in] vol  Volume handle, or NULL.
/// @return Open file count (0 if vol is NULL).
uint8_t VolGetOpenFileCount( VolRef vol ) {
    return vol ? vol->openFileCount : 0;
}

/// @brief Increment the open-file reference count for a volume.
/// @param[in,out] vol  Volume handle (safe no-op if NULL).
void VolIncrementOpenFiles( VolRef vol ) {
    if ( vol ) vol->openFileCount++;
}

/// @brief Decrement the open-file reference count for a volume.
/// @param[in,out] vol  Volume handle (safe no-op if NULL or already zero).
void VolDecrementOpenFiles( VolRef vol ) {
    if ( vol && vol->openFileCount > 0 ) vol->openFileCount--;
}

// ============================================================================
// Private helpers
// ============================================================================

/// @brief Allocate a free volume slot from the pool.
/// @return Pointer to a zeroed FSVolume, or NULL if all slots are in use.
static FSVolumePtr AllocVolume( void ) {
    for ( uint8_t i = 0; i < kVolMaxCount; i++ ) {
        if ( !( pool[ i ].statusBits & BIT_SHIFT( FlagVolMounted ) ) ) {
            memset( &pool[ i ], 0, sizeof( pool[ i ] ) );
            return &pool[ i ];
        }
    }
    return NULL;
}

/// @brief Check whether a volume should be mounted read-only.
/// @param[in] vol    Volume about to be mounted.
/// @param[in] entry  Partition entry for the volume.
/// @return true if either the mount flags or partition flags request read-only.
static bool IsReadOnly( const VolRef vol, const FSPartEntryPtr entry ) {
    if ( vol->mountFlags & FSMountReadOnly )          return true;
    if ( entry->flags & (uint8_t)FSPartFlagReadOnly ) return true;
    return false;
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Mount a LittleFS filesystem on a partition.
///
/// Resolves the partition geometry, allocates a volume slot, configures
/// the LittleFS instance, and mounts (or formats-and-mounts) the filesystem.
///
/// @param[in] part  Partition to mount.
/// @param[in] flags  Mount flags (e.g. FSMountReadOnly).
/// @return VolRef handle, or NULL on failure.
VolRef VolMount( PartRef part, FSMountFlags flags ) {
    if ( part == NULL ) return NULL;

    FSPartEntry entry;
    if ( PartGetEntry( part, &entry ) != FSResultOk ) return NULL;

    FSVolumePtr vol = AllocVolume();
    if ( vol == NULL ) return NULL;

    vol->partition      = part;
    vol->mountFlags     = flags;
    vol->openFileCount  = 0;
    vol->mountPoint[0]  = '/';
    strncpy( vol->mountPoint + 1, entry.name, kPartNameLen - 1 );
    vol->mountPoint[ kPartNameLen ] = '\0';

    LFSAdapterConfigure( &vol->lfsCfg, vol, entry.blockCount,
                         vol->readBuf, vol->progBuf, vol->lookaheadBuf );

    int err = lfs_mount( &vol->lfs, &vol->lfsCfg );
    if ( err != LFS_ERR_OK ) {
        lfs_format( &vol->lfs, &vol->lfsCfg );
        err = lfs_mount( &vol->lfs, &vol->lfsCfg );
        if ( err != LFS_ERR_OK ) return NULL;
        vol->statusBits |= BIT_SHIFT( FlagVolFormatted );
    }

    vol->statusBits |= BIT_SHIFT( FlagVolMounted );
    if ( IsReadOnly( vol, &entry ) ) {
        vol->statusBits |= BIT_SHIFT( FlagVolReadOnly );
    }

    return vol;
}

/// @brief Unmount a volume and release its LittleFS instance.
/// @param[in,out] vol  Volume to unmount.
/// @return FSResultOk on success, or FSResultInvalid / FSResultNotMounted / FSResultBusy.
FSResult VolUnmount( VolRef vol ) {
    if ( vol == NULL )                                return FSResultInvalid;
    if ( !( vol->statusBits & BIT_SHIFT( FlagVolMounted ) ) ) return FSResultNotMounted;
    if ( vol->openFileCount > 0 )                     return FSResultBusy;

    lfs_unmount( &vol->lfs );
    vol->statusBits = 0;
    return FSResultOk;
}

/// @brief Format a mounted volume, destroying all data.
///
/// Unmounts, formats, and remounts the filesystem. Only UID 0 (System)
/// may format a volume.
///
/// @param[in,out] vol  Volume to format.
/// @param[in] uid      Caller's UID (must be 0).
/// @return FSResultOk on success, or an FSResult error code.
FSResult VolFormat( VolRef vol, FSUid uid ) {
    if ( vol == NULL ) return FSResultInvalid;
    if ( uid != 0 )    return FSResultPermission;
    if ( !( vol->statusBits & BIT_SHIFT( FlagVolMounted ) ) ) return FSResultNotMounted;
    if ( vol->openFileCount > 0 ) return FSResultBusy;

    lfs_unmount( &vol->lfs );

    FSPartEntry entry;
    PartGetEntry( vol->partition, &entry );
    LFSAdapterConfigure( &vol->lfsCfg, vol, entry.blockCount,
                         vol->readBuf, vol->progBuf, vol->lookaheadBuf );

    int err = lfs_format( &vol->lfs, &vol->lfsCfg );
    if ( err != LFS_ERR_OK ) return FSMapLFSError( err );

    err = lfs_mount( &vol->lfs, &vol->lfsCfg );
    if ( err != LFS_ERR_OK ) {
        vol->statusBits &= ~BIT_SHIFT( FlagVolMounted );
        return FSMapLFSError( err );
    }

    vol->statusBits |= BIT_SHIFT( FlagVolFormatted );
    return FSResultOk;
}

/// @brief Return the status bits for a volume.
/// @param[in] vol  Volume handle, or NULL.
/// @return Status bitmask (0 if vol is NULL).
uint32_t VolGetStatus( VolRef vol ) {
    return vol ? vol->statusBits : 0;
}

/// @brief Return the partition backing a volume.
/// @param[in] vol  Volume handle, or NULL.
/// @return PartRef handle, or NULL if vol is NULL.
PartRef VolGetPartition( VolRef vol ) {
    return vol ? vol->partition : NULL;
}

/// @brief Resolve an absolute path to the volume that owns it.
///
/// Matches the leading path component against mounted volume mount points.
///
/// @param[in]  absPath  Absolute path, e.g. "/system/SysConfig.ini".
/// @param[out] relPath  Set to the volume-relative path on success.
/// @return Mounted VolRef handle, or NULL if no matching volume is found.
VolRef VolResolve( const char* absPath, const char** relPath ) {
    if ( absPath == NULL || relPath == NULL ) return NULL;
    for ( uint8_t i = 0; i < kVolMaxCount; i++ ) {
        if ( !( pool[ i ].statusBits & BIT_SHIFT( FlagVolMounted ) ) ) continue;
        const char* mp    = pool[ i ].mountPoint;
        size_t      mpLen = strlen( mp );
        if ( strncmp( absPath, mp, mpLen ) == 0 &&
             ( absPath[ mpLen ] == '/' || absPath[ mpLen ] == '\0' ) ) {
            const char* rem = absPath + mpLen;
            if ( *rem == '/' ) rem++;
            *relPath = ( *rem == '\0' ) ? "/" : rem;
            return &pool[ i ];
        }
    }
    return NULL;
}

#endif // FEATURE_FILE_SYSTEM
