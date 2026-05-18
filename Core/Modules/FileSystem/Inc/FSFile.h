/// @file FSFile.h
///
/// @brief File and directory operations for the FileSystem subsystem.
///
/// All operations accept a caller UID. UID 0 (System) bypasses all permission
/// checks. Non-zero UIDs are evaluated against the file's owner UID and mode
/// bits on every open call; the resolved access is then cached in the open
/// handle so per-read/write checks are cheap.
///
/// Async variants (FileReadAsync, FileWriteAsync) return immediately and
/// fire an FSCallback when the operation completes. The callback is invoked
/// from task context — it may pass a semaphore handle, a task notification
/// target, or an event flag group as userData to integrate with any
/// FreeRTOS synchronisation mechanism.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FSFILE_H
#define FSFILE_H

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <stdint.h>
#include <stddef.h>
#include "FSTypes.h"

// ============================================================================
// Limits
// ============================================================================

/// @brief Maximum number of simultaneously open file handles (shared pool).
enum { kFileMaxOpen = 8 };

// ============================================================================
// Opaque handle
// ============================================================================

/// @brief Opaque handle to an open file.
typedef struct FSFileHandle* FileRef;

// ============================================================================
// Synchronous file operations
// ============================================================================

/// @brief Open a file by absolute path.
///
/// Resolves the path to the appropriate mounted volume, checks caller
/// permissions, and opens the underlying filesystem file. If FSOpenCreate
/// is set and the file does not exist, it is created with FSModeDefault
/// and owner @p uid. Returns NULL on error.
///
/// @param path  Absolute path (e.g. "/system/SysConfig.ini").
/// @param flags Access mode and option flags.
/// @param uid   Caller's UID. UID 0 bypasses permission checks.
/// @return File handle, or NULL on error.
FileRef FileOpen( const char* path, FSOpenFlags flags, FSUid uid );

/// @brief Close an open file, flushing any pending writes.
/// @param file Handle returned by FileOpen().
/// @return FSResultOk on success.
FSResult FileClose( FileRef file );

/// @brief Read up to @p size bytes from the current file position.
///
/// @param file  Handle returned by FileOpen().
/// @param buf   Destination buffer; must be at least @p size bytes.
/// @param size  Maximum bytes to read.
/// @param read  Set to the number of bytes actually read; 0 at end of file.
/// @return FSResultOk, or an error code. A short read is not an error.
FSResult FileRead( FileRef file, void* buf, size_t size, size_t* read );

/// @brief Write @p size bytes to the current file position.
///
/// @param file    Handle returned by FileOpen().
/// @param buf     Source buffer; must be at least @p size bytes.
/// @param size    Number of bytes to write.
/// @param written Set to the number of bytes actually written.
/// @return FSResultOk, or an error code.
FSResult FileWrite( FileRef file, const void* buf, size_t size, size_t* written );

/// @brief Move the file position.
///
/// @param file   Handle returned by FileOpen().
/// @param offset Byte offset relative to @p origin.
/// @param origin One of FSSeekSet, FSSeekCur, FSSeekEnd.
/// @param pos    Set to the resulting absolute byte position.
/// @return FSResultOk, or FSResultInvalid if the resulting position is negative.
FSResult FileSeek( FileRef file, int32_t offset, FSSeekOrigin origin, uint32_t* pos );

/// @brief Return the current byte position within a file.
///
/// @param file Handle returned by FileOpen().
/// @param pos  Set to the current absolute position.
/// @return FSResultOk.
FSResult FileTell( FileRef file, uint32_t* pos );

/// @brief Return file or directory metadata without opening the file.
///
/// @param path Absolute path.
/// @param uid  Caller's UID.
/// @param stat Struct to populate.
/// @return FSResultOk, FSResultNotFound, or FSResultPermission.
FSResult FileStat( const char* path, FSUid uid, FSStatPtr stat );

/// @brief Delete a file or empty directory.
///
/// @param path Absolute path.
/// @param uid  Caller's UID. Only the owner (or UID 0) may delete.
/// @return FSResultOk, FSResultPermission, FSResultNotFound, or FSResultNotEmpty.
FSResult FileDelete( const char* path, FSUid uid );

/// @brief Create a directory.
///
/// @param path Absolute path of the new directory.
/// @param uid  Caller's UID (becomes the directory owner).
/// @param mode Permission mode for the new directory.
/// @return FSResultOk, FSResultExists, FSResultPermission, or FSResultNoSpace.
FSResult FileMkdir( const char* path, FSUid uid, FSMode mode );

// ============================================================================
// Asynchronous file operations
// ============================================================================

/// @brief Asynchronous variant of FileRead().
///
/// Returns immediately. @p callback is invoked from task context on completion.
/// @p buf must remain valid until the callback fires.
///
/// @param file     Handle returned by FileOpen().
/// @param buf      Destination buffer.
/// @param size     Maximum bytes to read.
/// @param callback Completion callback.
/// @param userData Passed unchanged to @p callback.
/// @return FSResultOk if the operation was accepted, or an error if the handle
///         is invalid or a previous async operation is still in flight.
FSResult FileReadAsync ( FileRef file, void* buf, size_t size,
                         FSCallback callback, void* userData );

/// @brief Asynchronous variant of FileWrite().
///
/// Returns immediately. @p callback is invoked from task context on completion.
/// @p buf must remain valid until the callback fires.
///
/// @param file     Handle returned by FileOpen().
/// @param buf      Source buffer.
/// @param size     Number of bytes to write.
/// @param callback Completion callback.
/// @param userData Passed unchanged to @p callback.
/// @return FSResultOk if the operation was accepted, or an error if the handle
///         is invalid or a previous async operation is still in flight.
FSResult FileWriteAsync( FileRef file, const void* buf, size_t size,
                         FSCallback callback, void* userData );

// ============================================================================
// Diagnostics
// ============================================================================

/// @brief Return the last error recorded on a file handle.
/// @param file Handle returned by FileOpen().
/// @return The most recent non-OK FSResult, or FSResultOk if none.
FSResult FileGetLastError( FileRef file );

#endif // FEATURE_FILE_SYSTEM

#endif // FSFILE_H
