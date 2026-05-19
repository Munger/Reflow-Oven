/// @file FSUtils.h
///
/// @brief Filesystem utility helpers — path manipulation and single-call file I/O.
///
/// Divided into two layers:
///
///   Path utilities — pure string operations with no filesystem dependency.
///   They resolve and manipulate paths but never open the filesystem themselves.
///
///   File convenience wrappers — open/operate/close in one call, built on top
///   of the FSFile API. These reduce boilerplate for callers that only need to
///   read or write a whole file, check existence, or ensure a directory exists.
///
/// All functions that write to a buffer always NUL-terminate @p dst and return
/// false (rather than leaving it empty) when the result would overflow. Callers
/// can treat a false return as an unrecoverable path-too-long condition.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FSUTILS_H
#define FSUTILS_H

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <stddef.h>
#include <stdbool.h>
#include "FSTypes.h"

// ============================================================================
// PATH UTILITIES
// ============================================================================

/// @brief Build an absolute path from a directory and a filename.
///
/// If @p name already begins with '/' it is copied to @p dst verbatim and
/// @p dir is ignored. Otherwise @p dir and @p name are joined with a single
/// '/', handling a trailing '/' in @p dir gracefully.
///
/// @param[out] dst     Destination buffer; always NUL-terminated on return.
/// @param[in]  dstSize Capacity of @p dst in bytes including the NUL terminator.
/// @param[in]  dir     Directory prefix; ignored if @p name is absolute.
/// @param[in]  name    Filename or absolute path.
/// @return true if the result fits in @p dst; false if it would overflow
///         (dst is set to an empty string in that case).
bool FSPathBuild( char* dst, size_t dstSize, const char* dir, const char* name );

/// @brief Return a pointer to the filename component of @p path.
///
/// Points to the character after the last '/', or to @p path itself if there
/// is no '/'. The returned pointer is into the original string — not a copy.
///
/// @param[in] path  Absolute or relative path.
/// @return Pointer to the filename component within @p path.
const char* FSPathBasename( const char* path );

/// @brief Return a pointer to the file extension of @p path, including the dot.
///
/// Searches only within the filename component (after the last '/'). Returns
/// NULL if there is no '.', or if the only '.' is a leading dot (hidden file).
///
/// @param[in] path  Absolute or relative path.
/// @return Pointer to the '.' that starts the extension, or NULL.
const char* FSPathExtension( const char* path );

/// @brief Copy the directory component of @p path into @p dst.
///
/// Everything up to (but not including) the last '/' is copied. If @p path
/// contains no '/', @p dst is set to ".". The result is always NUL-terminated.
///
/// @param[out] dst     Destination buffer.
/// @param[in]  dstSize Capacity of @p dst in bytes.
/// @param[in]  path    Absolute or relative path.
/// @return true if the result fits; false on overflow (dst set to empty string).
bool FSPathDirname( char* dst, size_t dstSize, const char* path );

/// @brief Test whether @p path is an absolute path (begins with '/').
///
/// @param[in] path  Path string; must not be NULL.
/// @return true if @p path begins with '/'.
bool FSPathIsAbsolute( const char* path );

// ============================================================================
// FILE CONVENIENCE WRAPPERS
// ============================================================================

/// @brief Read an entire file into @p buf in one call.
///
/// Opens @p path read-only, reads up to @p maxLen bytes, closes the file, and
/// stores the number of bytes actually read in @p bytesRead. A file larger than
/// @p maxLen is read up to the buffer limit without error — the caller can
/// detect truncation by comparing @p bytesRead to @p maxLen.
///
/// @param[in]  path      Absolute path.
/// @param[in]  uid       Caller UID; 0 bypasses permission checks.
/// @param[out] buf       Destination buffer; must be at least @p maxLen bytes.
/// @param[in]  maxLen    Maximum bytes to read.
/// @param[out] bytesRead Set to the number of bytes actually read; may be NULL.
/// @return FSResultOk, FSResultNotFound, FSResultPermission, or FSResultIO.
FSResult FSReadFile( const char* path, FSUid uid,
                     void* buf, size_t maxLen, size_t* bytesRead );

/// @brief Write @p len bytes from @p buf to a file, creating or truncating it.
///
/// Opens @p path for writing, creating it if absent and truncating it to zero
/// if it already exists, writes @p buf, then closes the file.
///
/// @param[in] path  Absolute path.
/// @param[in] uid   Caller UID; 0 bypasses permission checks.
/// @param[in] buf   Source data; must be at least @p len bytes.
/// @param[in] len   Number of bytes to write.
/// @return FSResultOk, FSResultPermission, FSResultNoSpace, or FSResultIO.
FSResult FSWriteFile( const char* path, FSUid uid,
                      const void* buf, size_t len );

/// @brief Append @p len bytes from @p buf to a file, creating it if absent.
///
/// @param[in] path  Absolute path.
/// @param[in] uid   Caller UID; 0 bypasses permission checks.
/// @param[in] buf   Source data; must be at least @p len bytes.
/// @param[in] len   Number of bytes to write.
/// @return FSResultOk, FSResultPermission, FSResultNoSpace, or FSResultIO.
FSResult FSAppendFile( const char* path, FSUid uid,
                       const void* buf, size_t len );

/// @brief Test whether a file or directory exists and is accessible.
///
/// @param[in] path  Absolute path.
/// @param[in] uid   Caller UID.
/// @return true if @p path exists and @p uid has at least read permission.
bool FSExists( const char* path, FSUid uid );

/// @brief Return the size in bytes of a file.
///
/// @param[in] path  Absolute path.
/// @param[in] uid   Caller UID.
/// @return File size in bytes, or a negative FSResult on error.
int32_t FSGetSize( const char* path, FSUid uid );

/// @brief Ensure a directory exists, creating it if necessary.
///
/// Calls FileMkdir() and treats FSResultExists as success. Does not create
/// intermediate directories — the parent must already exist.
///
/// @param[in] path  Absolute path of the directory to create or verify.
/// @param[in] uid   Caller UID.
/// @return FSResultOk, FSResultPermission, FSResultNoSpace, or FSResultIO.
FSResult FSEnsureDir( const char* path, FSUid uid );

#endif // FEATURE_FILE_SYSTEM

#endif // FSUTILS_H
