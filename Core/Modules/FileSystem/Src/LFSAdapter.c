/// @file LFSAdapter.c
///
/// @brief LittleFS block device adapter.
///
/// Provides the four lfs_config callbacks (read, prog, erase, sync) that
/// translate between LittleFS's block-relative addressing and the partition-
/// relative block device operations. The lfs_config.context pointer must be
/// set to the FSVolume that owns the lfs_t, which is done by Volume.c.
///
/// Also provides FSMapLFSError(), which converts LittleFS negative error codes
/// to FSResult values and is used throughout the FileSystem module.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include "lfs.h"
#include "FSInternal.h"
#include "Partition.h"
#include "BlockDevice.h"

// ============================================================================
// Error mapping
// ============================================================================

FSResult FSMapLFSError( int lfsErr ) {
    if ( lfsErr >= 0 )           return FSResultOk;
    switch ( lfsErr ) {
        case LFS_ERR_IO:         return FSResultIO;
        case LFS_ERR_CORRUPT:    return FSResultCorrupt;
        case LFS_ERR_NOENT:      return FSResultNotFound;
        case LFS_ERR_EXIST:      return FSResultExists;
        case LFS_ERR_NOTDIR:     return FSResultNotDir;
        case LFS_ERR_ISDIR:      return FSResultIsDir;
        case LFS_ERR_NOTEMPTY:   return FSResultNotEmpty;
        case LFS_ERR_NOSPC:      return FSResultNoSpace;
        case LFS_ERR_INVAL:      return FSResultInvalid;
        case LFS_ERR_NOMEM:      return FSResultNoMemory;
        default:                 return FSResultIO;
    }
}

// ============================================================================
// LittleFS block device callbacks
// ============================================================================
//
// LittleFS passes block numbers relative to the start of the partition
// (block 0 = first block of the partition). We offset by entry.startBlock
// to get the absolute block number on the physical device.

static int LFSRead( const struct lfs_config* c,
                    lfs_block_t block, lfs_off_t off,
                    void* buf, lfs_size_t size ) {
    VolRef vol = c->context;
    FSPartEntry entry;
    PartGetEntry( VolGetPartition( vol ), &entry );
    BDRef bd = PartGetDevice( VolGetPartition( vol ) );
    FSResult r = BDRead( bd, entry.startBlock + (uint32_t)block,
                         (uint32_t)off, buf, (uint32_t)size );
    return ( r == FSResultOk ) ? 0 : LFS_ERR_IO;
}

static int LFSProg( const struct lfs_config* c,
                    lfs_block_t block, lfs_off_t off,
                    const void* buf, lfs_size_t size ) {
    VolRef vol = c->context;
    FSPartEntry entry;
    PartGetEntry( VolGetPartition( vol ), &entry );
    BDRef bd = PartGetDevice( VolGetPartition( vol ) );
    FSResult r = BDProg( bd, entry.startBlock + (uint32_t)block,
                         (uint32_t)off, buf, (uint32_t)size );
    return ( r == FSResultOk ) ? 0 : LFS_ERR_IO;
}

static int LFSErase( const struct lfs_config* c, lfs_block_t block ) {
    VolRef vol = c->context;
    FSPartEntry entry;
    PartGetEntry( VolGetPartition( vol ), &entry );
    BDRef bd = PartGetDevice( VolGetPartition( vol ) );
    FSResult r = BDErase( bd, entry.startBlock + (uint32_t)block );
    return ( r == FSResultOk ) ? 0 : LFS_ERR_IO;
}

static int LFSSync( const struct lfs_config* c ) {
    VolRef vol = c->context;
    BDRef  bd  = PartGetDevice( VolGetPartition( vol ) );
    FSResult r = BDSync( bd );
    return ( r == FSResultOk ) ? 0 : LFS_ERR_IO;
}

// ============================================================================
// Config builder — called by Volume.c
// ============================================================================

/// @brief Populate an lfs_config for the given partition geometry and volume.
///
/// The caller (Volume.c) provides the static buffer pointers embedded in the
/// FSVolume struct. This function fills every field of @p cfg; the caller then
/// passes @p cfg to lfs_mount() or lfs_format().
///
/// @param cfg         lfs_config struct to populate.
/// @param vol         Volume that will own this lfs instance (stored as context).
/// @param blockCount  Number of blocks in the partition.
/// @param readBuf     Static read cache buffer (kLfsCacheSize bytes).
/// @param progBuf     Static program cache buffer (kLfsCacheSize bytes).
/// @param lookahead   Static lookahead buffer (kLfsLookahead bytes).
void LFSAdapterConfigure( struct lfs_config* cfg,
                           VolRef             vol,
                           uint32_t           blockCount,
                           uint8_t*           readBuf,
                           uint8_t*           progBuf,
                           uint8_t*           lookahead ) {
    cfg->context          = vol;
    cfg->read             = LFSRead;
    cfg->prog             = LFSProg;
    cfg->erase            = LFSErase;
    cfg->sync             = LFSSync;
    cfg->read_size        = 1;
    cfg->prog_size        = 256;
    cfg->block_size       = 4096;
    cfg->block_count      = blockCount;
    cfg->cache_size       = 256;
    cfg->lookahead_size   = 16;
    cfg->block_cycles     = 500;
    cfg->read_buffer      = readBuf;
    cfg->prog_buffer      = progBuf;
    cfg->lookahead_buffer = lookahead;
}

#endif // FEATURE_FILE_SYSTEM
