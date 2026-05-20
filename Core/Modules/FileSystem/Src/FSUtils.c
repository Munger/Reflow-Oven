/// @file FSUtils.c
///
/// @brief Filesystem utility helpers — implementation.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <string.h>
#include "FSUtils.h"
#include "FSFile.h"

// ============================================================================
// Path utilities
// ============================================================================

/// @brief Join a directory path and filename into an absolute path.
/// @param[out] dst     Destination buffer.
/// @param[in]  dstSize Size of the destination buffer.
/// @param[in]  dir     Directory path (ignored if name is absolute).
/// @param[in]  name    Filename or path component.
/// @return true if the path was written, false on truncation or NULL inputs.
bool FSPathBuild( char* dst, size_t dstSize, const char* dir, const char* name ) {
    if ( !dst || dstSize == 0 ) return false;
    dst[ 0 ] = '\0';
    if ( !name ) return false;

    if ( name[ 0 ] == '/' ) {
        size_t len = strlen( name );
        if ( len + 1 > dstSize ) return false;
        memcpy( dst, name, len + 1 );
        return true;
    }

    if ( !dir ) return false;

    size_t dirLen  = strlen( dir );
    bool   hasSlash = ( dirLen > 0 && dir[ dirLen - 1 ] == '/' );
    size_t nameLen = strlen( name );
    size_t needed  = dirLen + ( hasSlash ? 0 : 1 ) + nameLen + 1;

    if ( needed > dstSize ) return false;

    memcpy( dst, dir, dirLen );
    if ( !hasSlash ) dst[ dirLen++ ] = '/';
    memcpy( dst + dirLen, name, nameLen + 1 );
    return true;
}

/// @brief Return a pointer to the final component of a path.
/// @param[in] path  Path string (e.g. "/system/config.ini").
/// @return Pointer within @p path past the last '/' separator.
const char* FSPathBasename( const char* path ) {
    if ( !path ) return path;
    const char* last = path;
    for ( const char* p = path; *p; p++ ) {
        if ( *p == '/' ) last = p + 1;
    }
    return last;
}

/// @brief Return a pointer to the file extension (including the dot).
/// @param[in] path  Path string.
/// @return Pointer to the last '.' in the basename, or NULL.
const char* FSPathExtension( const char* path ) {
    if ( !path ) return NULL;
    const char* base = FSPathBasename( path );
    if ( base[ 0 ] == '.' ) base++;  // skip leading dot (hidden file / ".gitignore")
    const char* dot = NULL;
    for ( const char* p = base; *p; p++ ) {
        if ( *p == '.' ) dot = p;
    }
    return dot;
}

/// @brief Extract the parent directory path from a path.
/// @param[out] dst     Destination buffer.
/// @param[in]  dstSize Size of the destination buffer.
/// @param[in]  path    Path string.
/// @return true on success, false on truncation or NULL inputs.
bool FSPathDirname( char* dst, size_t dstSize, const char* path ) {
    if ( !dst || dstSize == 0 ) return false;
    dst[ 0 ] = '\0';
    if ( !path ) return false;

    const char* last = NULL;
    for ( const char* p = path; *p; p++ ) {
        if ( *p == '/' ) last = p;
    }

    if ( !last ) {
        if ( dstSize < 2 ) return false;
        dst[ 0 ] = '.';
        dst[ 1 ] = '\0';
        return true;
    }

    size_t len = (size_t)( last - path );
    if ( len == 0 ) len = 1;  // root '/'
    if ( len + 1 > dstSize ) return false;
    memcpy( dst, path, len );
    dst[ len ] = '\0';
    return true;
}

/// @brief Check whether a path starts with '/'.
/// @param[in] path  Path string.
/// @return true if @p path starts with '/'.
bool FSPathIsAbsolute( const char* path ) {
    return path != NULL && path[ 0 ] == '/';
}

// ============================================================================
// File convenience wrappers
// ============================================================================

/// @brief Read an entire file into a buffer (convenience wrapper).
/// @param[in]  path      Absolute path to the file.
/// @param[in]  uid       Effective UID for permission checks.
/// @param[out] buf       Destination buffer.
/// @param[in]  maxLen    Maximum number of bytes to read.
/// @param[out] bytesRead Set to the number of bytes actually read (may be NULL).
/// @return FSResultOk on success, or an FSResult error code.
FSResult FSReadFile( const char* path, FSUid uid,
                     void* buf, size_t maxLen, size_t* bytesRead ) {
    if ( bytesRead ) *bytesRead = 0;
    FileRef f = FileOpen( path, FSOpenReadOnly, uid );
    if ( !f ) return FSResultNotFound;

    size_t   read   = 0;
    FSResult result = FileRead( f, buf, maxLen, &read );
    if ( bytesRead ) *bytesRead = read;

    FileClose( f );
    return result;
}

/// @brief Write an entire buffer to a file (convenience wrapper).
/// @param[in] path  Absolute path to the file (created or truncated).
/// @param[in] uid   Effective UID for permission checks.
/// @param[in] buf   Source data.
/// @param[in] len   Number of bytes to write.
/// @return FSResultOk on success, or an FSResult error code.
FSResult FSWriteFile( const char* path, FSUid uid,
                      const void* buf, size_t len ) {
    FileRef f = FileOpen( path, FSOpenWriteOnly | FSOpenCreate | FSOpenTruncate, uid );
    if ( !f ) return FSResultPermission;

    size_t   written = 0;
    FSResult result  = FileWrite( f, buf, len, &written );
    if ( result == FSResultOk && written != len ) result = FSResultIO;

    FileClose( f );
    return result;
}

/// @brief Append a buffer to a file (convenience wrapper).
/// @param[in] path  Absolute path to the file (created if absent).
/// @param[in] uid   Effective UID for permission checks.
/// @param[in] buf   Source data.
/// @param[in] len   Number of bytes to append.
/// @return FSResultOk on success, or an FSResult error code.
FSResult FSAppendFile( const char* path, FSUid uid,
                       const void* buf, size_t len ) {
    FileRef f = FileOpen( path, FSOpenWriteOnly | FSOpenCreate | FSOpenAppend, uid );
    if ( !f ) return FSResultPermission;

    size_t   written = 0;
    FSResult result  = FileWrite( f, buf, len, &written );
    if ( result == FSResultOk && written != len ) result = FSResultIO;

    FileClose( f );
    return result;
}

/// @brief Check whether a file or directory exists.
/// @param[in] path  Absolute path to check.
/// @param[in] uid   Effective UID (used for stat permission, currently reserved).
/// @return true if the path exists and is accessible.
bool FSExists( const char* path, FSUid uid ) {
    FSStat stat;
    return FileStat( path, uid, &stat ) == FSResultOk;
}

/// @brief Get the size of a file in bytes.
/// @param[in] path  Absolute path to the file.
/// @param[in] uid   Effective UID (used for stat permission, currently reserved).
/// @return File size in bytes, or a negative FSResult error code on failure.
int32_t FSGetSize( const char* path, FSUid uid ) {
    FSStat stat;
    FSResult result = FileStat( path, uid, &stat );
    if ( result != FSResultOk ) return (int32_t)result;
    return (int32_t)stat.size;
}

/// @brief Ensure a directory exists, creating it if necessary.
/// @param[in] path  Absolute path to the directory.
/// @param[in] uid   Effective UID and owner for the new directory.
/// @return FSResultOk on success (exists or created), or an FSResult error code.
FSResult FSEnsureDir( const char* path, FSUid uid ) {
    FSResult result = FileMkdir( path, uid, FSModeDirDefault );
    return ( result == FSResultExists ) ? FSResultOk : result;
}

#endif // FEATURE_FILE_SYSTEM
