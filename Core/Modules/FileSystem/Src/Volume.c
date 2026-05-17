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

#include <string.h>
#include "Volume.h"
#include "FSInternal.h"
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
    lfs_t             lfs;
    struct lfs_config lfsCfg;
    uint8_t           readBuf[ kLfsCacheSize ];
    uint8_t           progBuf[ kLfsCacheSize ];
    uint8_t           lookaheadBuf[ kLfsLookahead ];
} FSVolume;

#define BIT_SHIFT( n ) ( 1UL << (uint32_t)( n ) )

static FSVolume pool[ kVolMaxCount ];

// ============================================================================
// Package-private accessors (declared in FSInternal.h)
// ============================================================================

lfs_t* VolGetLFS( VolRef vol ) {
    if ( vol == NULL ) return NULL;
    if ( !( vol->statusBits & BIT_SHIFT( FlagVolMounted ) ) ) return NULL;
    return &vol->lfs;
}

uint8_t VolGetOpenFileCount( VolRef vol ) {
    return vol ? vol->openFileCount : 0;
}

void VolIncrementOpenFiles( VolRef vol ) {
    if ( vol ) vol->openFileCount++;
}

void VolDecrementOpenFiles( VolRef vol ) {
    if ( vol && vol->openFileCount > 0 ) vol->openFileCount--;
}

// ============================================================================
// Private helpers
// ============================================================================

#define BIT_SHIFT( n ) ( 1UL << (uint32_t)( n ) )

static FSVolume* AllocVolume( void ) {
    for ( uint8_t i = 0; i < kVolMaxCount; i++ ) {
        if ( !( pool[ i ].statusBits & BIT_SHIFT( FlagVolMounted ) ) ) {
            memset( &pool[ i ], 0, sizeof( pool[ i ] ) );
            return &pool[ i ];
        }
    }
    return NULL;
}

static bool IsReadOnly( const VolRef vol, const FSPartEntry* entry ) {
    if ( vol->mountFlags & FSMountReadOnly )          return true;
    if ( entry->flags & (uint8_t)FSPartFlagReadOnly ) return true;
    return false;
}

// ============================================================================
// Public API
// ============================================================================

VolRef VolMount( PartRef part, FSMountFlags flags ) {
    if ( part == NULL ) return NULL;

    FSPartEntry entry;
    if ( PartGetEntry( part, &entry ) != FSResultOk ) return NULL;

    FSVolume* vol = AllocVolume();
    if ( vol == NULL ) return NULL;

    vol->partition      = part;
    vol->mountFlags     = flags;
    vol->openFileCount  = 0;

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

FSResult VolUnmount( VolRef vol ) {
    if ( vol == NULL )                                return FSResultInvalid;
    if ( !( vol->statusBits & BIT_SHIFT( FlagVolMounted ) ) ) return FSResultNotMounted;
    if ( vol->openFileCount > 0 )                     return FSResultBusy;

    lfs_unmount( &vol->lfs );
    vol->statusBits = 0;
    return FSResultOk;
}

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

uint32_t VolGetStatus( VolRef vol ) {
    return vol ? vol->statusBits : 0;
}

PartRef VolGetPartition( VolRef vol ) {
    return vol ? vol->partition : NULL;
}
