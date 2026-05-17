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
} FSFileHandle, *FSFileHandlePtr;

static FSFileHandle handlePool[ kFileMaxOpen ];

// ============================================================================
// Private helpers
// ============================================================================

static FSFileHandlePtr AllocHandle( void ) {
    for ( uint8_t i = 0; i < kFileMaxOpen; i++ ) {
        if ( !handlePool[ i ].inUse ) {
            memset( &handlePool[ i ], 0, sizeof( handlePool[ i ] ) );
            handlePool[ i ].inUse = true;
            return &handlePool[ i ];
        }
    }
    return NULL;
}

static void FreeHandle( FSFileHandlePtr h ) {
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

// ============================================================================
// Per-file attribute storage (LittleFS custom attributes)
// ============================================================================
//
// LittleFS has no native uid/mode fields. We store them as two custom
// attributes on every file and directory so permission checks are real.

enum { kAttrUid  = 0x01 };  // stored as 1 byte
enum { kAttrMode = 0x02 };  // stored as 2 bytes (little-endian)

static void GetFileAttrs( lfs_t* lfs, const char* path,
                          FSUid* uid, FSMode* mode ) {
    uint8_t  u = 0;
    uint16_t m;
    lfs_getattr( lfs, path, kAttrUid,  &u, sizeof(u) );
    *uid = (FSUid)u;
    if ( lfs_getattr( lfs, path, kAttrMode, &m, sizeof(m) ) >= 0 ) {
        *mode = (FSMode)m;
    }
    // If kAttrMode is absent, *mode retains the caller's default
}

static FSResult SetFileAttrs( lfs_t* lfs, const char* path,
                               FSUid uid, FSMode mode ) {
    uint8_t  u = (uint8_t)uid;
    uint16_t m = (uint16_t)mode;
    int err = lfs_setattr( lfs, path, kAttrUid,  &u, sizeof(u) );
    if ( err != LFS_ERR_OK ) return FSMapLFSError( err );
    err = lfs_setattr( lfs, path, kAttrMode, &m, sizeof(m) );
    return FSMapLFSError( err );
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

FileRef FileOpen( const char* path, FSOpenFlags flags, FSUid uid ) {
    if ( path == NULL ) return NULL;

    const char* relPath;
    VolRef vol = VolResolve( path, &relPath );
    if ( vol == NULL ) return NULL;

    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return NULL;

    bool wantWrite = ( flags & 0x03 ) != FSOpenReadOnly;

    if ( wantWrite && ( VolGetStatus( vol ) & ( 1UL << FlagVolReadOnly ) ) ) {
        return NULL;
    }

    bool   fileExists = false;
    FSUid  ownerUid   = 0;
    FSMode fileMode   = FSModeDefault;

    struct lfs_info info;
    if ( lfs_stat( lfs, relPath, &info ) == LFS_ERR_OK ) {
        fileExists = true;
        GetFileAttrs( lfs, relPath, &ownerUid, &fileMode );
        if ( !HasReadPermission( uid, ownerUid, fileMode ) ) return NULL;
        if ( wantWrite && !HasWritePermission( uid, ownerUid, fileMode ) ) return NULL;
    } else if ( !( flags & FSOpenCreate ) ) {
        return NULL;
    }

    FSFileHandlePtr h = AllocHandle();
    if ( h == NULL ) return NULL;

    h->volume    = vol;
    h->uid       = uid;
    h->canWrite  = wantWrite;
    h->lastError = FSResultOk;

    h->lfsFileCfg.buffer = h->fileBuf;

    int err = lfs_file_opencfg( lfs, &h->lfsFile, relPath,
                                 MapOpenFlags( flags ), &h->lfsFileCfg );
    if ( err != LFS_ERR_OK ) {
        h->lastError = FSMapLFSError( err );
        FreeHandle( h );
        return NULL;
    }

    if ( !fileExists ) {
        SetFileAttrs( lfs, relPath, uid, FSModeDefault );
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

FSResult FileStat( const char* path, FSUid uid, FSStatPtr stat ) {
    if ( path == NULL || stat == NULL ) return FSResultInvalid;
    const char* relPath;
    VolRef vol = VolResolve( path, &relPath );
    if ( vol == NULL ) return FSResultNotFound;
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;

    struct lfs_info info;
    int err = lfs_stat( lfs, relPath, &info );
    if ( err != LFS_ERR_OK ) return FSMapLFSError( err );

    FSUid  ownerUid = 0;
    FSMode fileMode = ( info.type == LFS_TYPE_DIR ) ? FSModeDirDefault : FSModeDefault;
    GetFileAttrs( lfs, relPath, &ownerUid, &fileMode );
    (void)uid;
    stat->size  = (uint32_t)info.size;
    stat->mode  = fileMode;
    stat->uid   = ownerUid;
    stat->isDir = ( info.type == LFS_TYPE_DIR );
    return FSResultOk;
}

FSResult FileDelete( const char* path, FSUid uid ) {
    if ( path == NULL ) return FSResultInvalid;
    const char* relPath;
    VolRef vol = VolResolve( path, &relPath );
    if ( vol == NULL ) return FSResultNotFound;
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;

    if ( uid != 0 ) {
        FSUid  ownerUid = 0;
        FSMode fileMode = FSModeDefault;
        GetFileAttrs( lfs, relPath, &ownerUid, &fileMode );
        if ( uid != ownerUid ) return FSResultPermission;
    }

    return FSMapLFSError( lfs_remove( lfs, relPath ) );
}

FSResult FileMkdir( const char* path, FSUid uid, FSMode mode ) {
    if ( path == NULL ) return FSResultInvalid;
    const char* relPath;
    VolRef vol = VolResolve( path, &relPath );
    if ( vol == NULL ) return FSResultNotFound;
    lfs_t* lfs = VolGetLFS( vol );
    if ( lfs == NULL ) return FSResultNotMounted;
    int err = lfs_mkdir( lfs, relPath );
    if ( err != LFS_ERR_OK ) return FSMapLFSError( err );
    SetFileAttrs( lfs, relPath, uid, mode );
    return FSResultOk;
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
