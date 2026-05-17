/// @file FSFile.c
///
/// @brief File and directory operations implementation.
///
/// Maintains a shared pool of FSFileHandle instances. Each handle wraps a
/// lfs_file_t and its per-file cache buffer. Permission checks happen at
/// open time; once a handle is allocated the effective access is recorded
/// in the handle and not re-checked per read/write.
///
/// Async operations currently run synchronously on the calling task and then
/// invoke the callback before returning. A future dedicated FS task can take
/// over by queuing the operation struct and calling the callback on completion
/// — the public API will not need to change.
///
/// Permission model (simplified for current contexts):
///   UID 0 (System) — always permitted.
///   Owner (uid matches file uid) — owner bits applied.
///   Others — "other" bits applied.
///   Groups are not yet implemented; group bits are reserved.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>
#include "FSFile.h"
#include "FSInternal.h"
#include "lfs.h"

// ============================================================================
// Cache size (must match Volume.c / LFSAdapter.c)
// ============================================================================

enum { kFileCacheSize = 256 };

// ============================================================================
// Private handle type
// ============================================================================

typedef struct FSFileHandle {
    VolRef             volume;
    FSUid              uid;         ///< Caller's effective UID at open time
    bool               canWrite;    ///< Resolved from permissions at open
    bool               inUse;
    FSResult           lastError;
    lfs_file_t         lfsFile;
    struct lfs_file_config lfsFileCfg;
    uint8_t            fileBuf[ kFileCacheSize ];
} FSFileHandle;

static FSFileHandle handlePool[ kFileMaxOpen ];

// ============================================================================
// Private helpers
// ============================================================================

static FSFileHandle* AllocHandle( void ) {
    for ( uint8_t i = 0; i < kFileMaxOpen; i++ ) {
        if ( !handlePool[ i ].inUse ) {
            memset( &handlePool[ i ], 0, sizeof( handlePool[ i ] ) );
            handlePool[ i ].inUse = true;
            return &handlePool[ i ];
        }
    }
    return NULL;
}

static void FreeHandle( FSFileHandle* h ) {
    if ( h ) h->inUse = false;
}

static int MapOpenFlags( FSOpenFlags flags ) {
    int lfsFlags = 0;
    switch ( flags & 0x03 ) {
        case FSOpenReadOnly:  lfsFlags = LFS_O_RDONLY; break;
        case FSOpenWriteOnly: lfsFlags = LFS_O_WRONLY; break;
        case FSOpenReadWrite: lfsFlags = LFS_O_RDWR;   break;
        default:              lfsFlags = LFS_O_RDONLY; break;
    }
    if ( flags & FSOpenCreate   ) lfsFlags |= LFS_O_CREAT;
    if ( flags & FSOpenTruncate ) lfsFlags |= LFS_O_TRUNC;
    if ( flags & FSOpenAppend   ) lfsFlags |= LFS_O_APPEND;
    return lfsFlags;
}

/// @brief Return true if @p uid has read permission on a file with @p mode owned by @p ownerUid.
static bool HasReadPermission( FSUid uid, FSUid ownerUid, FSMode mode ) {
    if ( uid == 0 ) return true;
    if ( uid == ownerUid ) return ( mode & FSModeOwnerRead ) != 0;
    return ( mode & FSModeOtherRead ) != 0;
}

/// @brief Return true if @p uid has write permission on a file with @p mode owned by @p ownerUid.
static bool HasWritePermission( FSUid uid, FSUid ownerUid, FSMode mode ) {
    if ( uid == 0 ) return true;
    if ( uid == ownerUid ) return ( mode & FSModeOwnerWrite ) != 0;
    return ( mode & FSModeOtherWrite ) != 0;
}

// ============================================================================
// Public API — files
// ============================================================================

FileRef FileOpen( VolRef vol, const char* path, FSOpenFlags flags, FSUid uid ) {
    if ( vol == NULL || path == NULL ) return NULL;

    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return NULL;

    bool wantWrite = ( flags & 0x03 ) != FSOpenReadOnly;

    if ( !( flags & FSOpenCreate ) ) {
        struct lfs_info info;
        int err = lfs_stat( lfs, path, &info );
        if ( err == LFS_ERR_OK ) {
            FSUid  ownerUid = 0;       // LittleFS has no native uid — default to 0
            FSMode fileMode = FSModeDefault;
            if ( !HasReadPermission( uid, ownerUid, fileMode ) ) return NULL;
            if ( wantWrite && !HasWritePermission( uid, ownerUid, fileMode ) ) return NULL;
        }
    }

    FSFileHandle* h = AllocHandle();
    if ( h == NULL ) return NULL;

    h->volume    = vol;
    h->uid       = uid;
    h->canWrite  = wantWrite;
    h->lastError = FSResultOk;

    h->lfsFileCfg.buffer = h->fileBuf;

    int err = lfs_file_opencfg( lfs, &h->lfsFile, path,
                                 MapOpenFlags( flags ), &h->lfsFileCfg );
    if ( err != LFS_ERR_OK ) {
        h->lastError = FSMapLFSError( err );
        FreeHandle( h );
        return NULL;
    }

    VolIncrementOpenFiles( vol );
    return h;
}

FSResult FileClose( FileRef file ) {
    if ( file == NULL || !file->inUse ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    int err = lfs_file_close( lfs, &file->lfsFile );
    VolDecrementOpenFiles( file->volume );
    FreeHandle( file );
    return FSMapLFSError( err );
}

FSResult FileRead( FileRef file, void* buf, size_t size, size_t* read ) {
    if ( file == NULL || buf == NULL || read == NULL ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    lfs_ssize_t n = lfs_file_read( lfs, &file->lfsFile, buf, (lfs_size_t)size );
    if ( n < 0 ) {
        file->lastError = FSMapLFSError( (int)n );
        return file->lastError;
    }
    *read = (size_t)n;
    return FSResultOk;
}

FSResult FileWrite( FileRef file, const void* buf, size_t size, size_t* written ) {
    if ( file == NULL || buf == NULL || written == NULL ) return FSResultInvalid;
    if ( !file->canWrite ) return FSResultPermission;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    lfs_ssize_t n = lfs_file_write( lfs, &file->lfsFile, buf, (lfs_size_t)size );
    if ( n < 0 ) {
        file->lastError = FSMapLFSError( (int)n );
        return file->lastError;
    }
    *written = (size_t)n;
    return FSResultOk;
}

FSResult FileSeek( FileRef file, int32_t offset, FSSeekOrigin origin, uint32_t* pos ) {
    if ( file == NULL || pos == NULL ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    lfs_soff_t p = lfs_file_seek( lfs, &file->lfsFile,
                                   (lfs_soff_t)offset, (int)origin );
    if ( p < 0 ) {
        file->lastError = FSMapLFSError( (int)p );
        return file->lastError;
    }
    *pos = (uint32_t)p;
    return FSResultOk;
}

FSResult FileTell( FileRef file, uint32_t* pos ) {
    if ( file == NULL || pos == NULL ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    lfs_soff_t p = lfs_file_tell( lfs, &file->lfsFile );
    if ( p < 0 ) return FSMapLFSError( (int)p );
    *pos = (uint32_t)p;
    return FSResultOk;
}

// ============================================================================
// Public API — directory and metadata
// ============================================================================

FSResult FileStat( VolRef vol, const char* path, FSUid uid, FSStat* stat ) {
    if ( vol == NULL || path == NULL || stat == NULL ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;

    struct lfs_info info;
    int err = lfs_stat( lfs, path, &info );
    if ( err != LFS_ERR_OK ) return FSMapLFSError( err );

    (void)uid;
    stat->size  = (uint32_t)info.size;
    stat->mode  = ( info.type == LFS_TYPE_DIR ) ? FSModeDirDefault : FSModeDefault;
    stat->uid   = 0;
    stat->isDir = ( info.type == LFS_TYPE_DIR );
    return FSResultOk;
}

FSResult FileDelete( VolRef vol, const char* path, FSUid uid ) {
    if ( vol == NULL || path == NULL ) return FSResultInvalid;
    if ( uid != 0 ) {
        // Non-root callers may only delete files they own.
        // LittleFS has no native ownership; treat as permission denied for non-root.
        return FSResultPermission;
    }
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;
    return FSMapLFSError( lfs_remove( lfs, path ) );
}

FSResult FileMkdir( VolRef vol, const char* path, FSUid uid, FSMode mode ) {
    if ( vol == NULL || path == NULL ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;
    (void)uid; (void)mode;
    return FSMapLFSError( lfs_mkdir( lfs, path ) );
}

// ============================================================================
// Async operations
// ============================================================================
//
// Current implementation: runs the synchronous operation on the calling task,
// then invokes the callback before returning. A dedicated FS task can take
// over by queuing an operation struct — the public API will not change.

FSResult FileReadAsync( FileRef file, void* buf, size_t size,
                        FSCallback callback, void* userData ) {
    size_t   bytesRead = 0;
    FSResult result    = FileRead( file, buf, size, &bytesRead );
    if ( callback ) callback( result, userData );
    return result;
}

FSResult FileWriteAsync( FileRef file, const void* buf, size_t size,
                         FSCallback callback, void* userData ) {
    size_t   written = 0;
    FSResult result  = FileWrite( file, buf, size, &written );
    if ( callback ) callback( result, userData );
    return result;
}

// ============================================================================
// Diagnostics
// ============================================================================

FSResult FileGetLastError( FileRef file ) {
    return file ? file->lastError : FSResultInvalid;
}
