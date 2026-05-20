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

#include "Features.h"

#if FEATURE_FILE_SYSTEM

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

/// @brief Allocate a file handle from the shared pool.
/// @return Pointer to a zeroed FSFileHandle marked in-use, or NULL if the pool is full.
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

/// @brief Return a file handle to the shared pool.
/// @param[in,out] h  Handle to free (may be NULL; safe no-op).
static void FreeHandle( FSFileHandlePtr h ) {
    if ( h ) h->inUse = false;
}

/// @brief Translate FSOpenFlags to LittleFS lfs_open flags.
/// @param[in] flags  FSOpenFlags bitmask.
/// @return LittleFS flags word suitable for lfs_file_opencfg().
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

/// @brief Read LittleFS custom attributes (uid, mode) for a file or directory.
/// @param[in]  lfs   Mounted LittleFS instance.
/// @param[in]  path  Absolute path within the volume.
/// @param[out] uid   Set to the file owner UID (0 if absent).
/// @param[out] mode  Set to the file permission mode (unchanged if absent).
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

/// @brief Write LittleFS custom attributes (uid, mode) for a file or directory.
/// @param[in] lfs   Mounted LittleFS instance.
/// @param[in] path  Absolute path within the volume.
/// @param[in] uid   Owner UID to store.
/// @param[in] mode  Permission mode to store.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Open or create a file on a mounted filesystem.
///
/// Resolves the absolute path to a volume, checks read/write permissions,
/// allocates a file handle, and opens the LittleFS file. If the file does
/// not exist and FSOpenCreate is set, it is created with default attributes.
///
/// @param[in] path   Absolute path to the file (e.g. "/system/config.ini").
/// @param[in] flags  Open-mode bitmask (read/write/create/truncate/append).
/// @param[in] uid    Effective UID of the caller (0 bypasses permission checks).
/// @return Opaque FileRef handle, or NULL on failure.
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

/// @brief Close an open file handle.
/// @param[in,out] file  Handle returned by FileOpen().
/// @return FSResultOk on success, or an FSResult error code.
FSResult FileClose( FileRef file ) {
    if ( file == NULL || !file->inUse ) return FSResultInvalid;
    lfs_t* lfs = VolGetLFS( file->volume );
    if ( lfs == NULL ) return FSResultNotMounted;

    int err = lfs_file_close( lfs, &file->lfsFile );
    VolDecrementOpenFiles( file->volume );
    FreeHandle( file );
    return FSMapLFSError( err );
}

/// @brief Read bytes from an open file at the current seek position.
/// @param[in]  file  Open file handle.
/// @param[out] buf   Destination buffer.
/// @param[in]  size  Number of bytes to read.
/// @param[out] read  Set to the number of bytes actually read.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Write bytes to an open file at the current seek position.
/// @param[in]  file    Open file handle with write permission.
/// @param[in]  buf     Source data.
/// @param[in]  size    Number of bytes to write.
/// @param[out] written Set to the number of bytes actually written.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Seek to a position in an open file.
/// @param[in]  file   Open file handle.
/// @param[in]  offset Offset relative to @p origin.
/// @param[in]  origin  SeekOrigin (start, current, or end).
/// @param[out] pos    Set to the resulting absolute position in the file.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Return the current read/write position in an open file.
/// @param[in]  file Open file handle.
/// @param[out] pos  Set to the current byte offset from the start of the file.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Stat a file or directory by absolute path.
///
/// Resolves the path to a volume and retrieves size, type, mode, and owner.
/// Permission checks on the stat operation itself are not enforced (any
/// caller can stat), but the returned uid can be used for later checks.
///
/// @param[in]  path Absolute path to the file or directory.
/// @param[in]  uid  Effective UID (currently unused, reserved for future filtering).
/// @param[out] stat Populated with size, mode, uid, and isDir.
/// @return FSResultOk on success, or FSResultNotFound / FSResultNotMounted.
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

/// @brief Delete a file or empty directory by absolute path.
/// @param[in] path Absolute path to the entry to remove.
/// @param[in] uid  Effective UID (only the owner or UID 0 may delete).
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Create a directory and set its owner and permissions.
/// @param[in] path Absolute path for the new directory.
/// @param[in] uid  Owner UID to assign.
/// @param[in] mode Permission mode (e.g. FSModeDirDefault).
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Asynchronously read from an open file.
///
/// Currently runs synchronously and invokes @p callback before returning.
/// A future dedicated FS task can queue the operation without changing the API.
///
/// @param[in]  file     Open file handle.
/// @param[out] buf      Destination buffer.
/// @param[in]  size     Number of bytes to read.
/// @param[in]  callback Called with the result and @p userData when the read completes.
/// @param[in]  userData User pointer passed through to the callback.
/// @return FSResultOk on success, or an FSResult error code.
FSResult FileReadAsync( FileRef file, void* buf, size_t size,
                        FSCallback callback, void* userData ) {
    size_t   bytesRead = 0;
    FSResult result    = FileRead( file, buf, size, &bytesRead );
    if ( callback ) callback( result, userData );
    return result;
}

/// @brief Asynchronously write to an open file.
///
/// Currently runs synchronously and invokes @p callback before returning.
/// A future dedicated FS task can queue the operation without changing the API.
///
/// @param[in]  file     Open file handle with write permission.
/// @param[in]  buf      Source data.
/// @param[in]  size     Number of bytes to write.
/// @param[in]  callback Called with the result and @p userData when the write completes.
/// @param[in]  userData User pointer passed through to the callback.
/// @return FSResultOk on success, or an FSResult error code.
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

/// @brief Return the last error recorded on a file handle.
/// @param[in] file File handle to query, or NULL.
/// @return The last FSResult error, or FSResultInvalid if file is NULL.
FSResult FileGetLastError( FileRef file ) {
    return file ? file->lastError : FSResultInvalid;
}

#endif // FEATURE_FILE_SYSTEM
