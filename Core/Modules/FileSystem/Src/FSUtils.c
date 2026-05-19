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

const char* FSPathBasename( const char* path ) {
    if ( !path ) return path;
    const char* last = path;
    for ( const char* p = path; *p; p++ ) {
        if ( *p == '/' ) last = p + 1;
    }
    return last;
}

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

bool FSPathIsAbsolute( const char* path ) {
    return path != NULL && path[ 0 ] == '/';
}

// ============================================================================
// File convenience wrappers
// ============================================================================

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

bool FSExists( const char* path, FSUid uid ) {
    FSStat stat;
    return FileStat( path, uid, &stat ) == FSResultOk;
}

int32_t FSGetSize( const char* path, FSUid uid ) {
    FSStat stat;
    FSResult result = FileStat( path, uid, &stat );
    if ( result != FSResultOk ) return (int32_t)result;
    return (int32_t)stat.size;
}

FSResult FSEnsureDir( const char* path, FSUid uid ) {
    FSResult result = FileMkdir( path, uid, FSModeDirDefault );
    return ( result == FSResultExists ) ? FSResultOk : result;
}

#endif // FEATURE_FILE_SYSTEM
