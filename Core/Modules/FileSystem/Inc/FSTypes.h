/// @file FSTypes.h
///
/// @brief Shared types, error codes, and callback signature for the FileSystem subsystem.
///
/// This header is the only FileSystem header with no dependencies on other
/// FileSystem headers. All other layers include this file.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FSTYPES_H
#define FSTYPES_H

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lfs.h"

// ============================================================================
// Filesystem limits
// ============================================================================

/// @brief Maximum length of a single filename component, excluding the NUL terminator.
///
/// Derived from LFS_NAME_MAX so that a change in the LittleFS configuration
/// propagates automatically. Buffers that hold a bare filename should be
/// declared as char buf[ kFSMaxNameLen + 1 ].
enum { kFSMaxNameLen = LFS_NAME_MAX };

/// @brief Maximum absolute path length accepted by this project, excluding the NUL terminator.
///
/// LittleFS imposes no path-length limit of its own — paths are walked one
/// component at a time. This constant is a project-level ceiling that sizes
/// stack and heap path buffers. Buffers that hold a full path should be
/// declared as char buf[ kFSMaxPathLen + 1 ].
enum { kFSMaxPathLen = 511 };

// ============================================================================
// Result codes
// ============================================================================

/// @brief Result codes returned by all FileSystem operations.
///
/// Negative values indicate errors; FSResultOk (0) indicates success.
typedef enum {
    FSResultOk          =   0, ///< Success
    FSResultIO          =  -1, ///< Hardware I/O error
    FSResultCorrupt     =  -2, ///< Filesystem data is corrupted
    FSResultNotFound    =  -3, ///< File or directory not found
    FSResultExists      =  -4, ///< File or directory already exists
    FSResultNotDir      =  -5, ///< Path component is not a directory
    FSResultIsDir       =  -6, ///< Target is a directory, not a file
    FSResultNotEmpty    =  -7, ///< Directory is not empty
    FSResultNoSpace     =  -8, ///< No space remaining on the volume
    FSResultInvalid     =  -9, ///< Invalid argument
    FSResultBusy        = -10, ///< Resource is busy (e.g. volume has open files)
    FSResultNoMemory    = -11, ///< Static resource pool exhausted
    FSResultPermission  = -12, ///< Caller lacks the required permission
    FSResultNotMounted  = -13, ///< Volume is not currently mounted
    FSResultNotReady    = -14, ///< Block device has not been initialised
} FSResult;

// ============================================================================
// Async callback
// ============================================================================

/// @brief Completion callback for asynchronous FileSystem operations.
///
/// Fired from task context when an async operation finishes. The @p userData
/// pointer is whatever the caller passed — a semaphore handle, a task handle,
/// an event flag group, or any other synchronisation primitive.
///
/// @param result   Outcome of the completed operation.
/// @param userData Caller-supplied context pointer.
typedef void ( *FSCallback )( FSResult result, void* userData );

// ============================================================================
// Identity and permissions
// ============================================================================

/// @brief User / context identifier.
///
/// UID 0 is the System context and bypasses all permission checks, matching
/// the Unix root convention. Non-zero UIDs are subject to normal enforcement.
typedef uint8_t FSUid;

/// @brief Unix-style permission bit field.
///
/// Bit layout (octal): owner rwx (bits 8–6), group rwx (bits 5–3),
/// other rwx (bits 2–0). Matches POSIX mode_t lower 9 bits.
typedef uint16_t FSMode;

/// @brief Standard permission bit constants (octal values).
enum {
    FSModeOwnerRead  = 0400, ///< Owner read
    FSModeOwnerWrite = 0200, ///< Owner write
    FSModeOwnerExec  = 0100, ///< Owner execute / directory traverse
    FSModeGroupRead  = 0040, ///< Group read
    FSModeGroupWrite = 0020, ///< Group write
    FSModeGroupExec  = 0010, ///< Group execute / traverse
    FSModeOtherRead  = 0004, ///< Others read
    FSModeOtherWrite = 0002, ///< Others write
    FSModeOtherExec  = 0001, ///< Others execute / traverse

    FSModeDefault    = 0644, ///< Owner rw, group r, others r — default for files
    FSModeDirDefault = 0755, ///< Owner rwx, group rx, others rx — default for directories
};

// ============================================================================
// File open flags and seek origin
// ============================================================================

/// @brief File open flags. Combine access mode with option flags using bitwise OR.
typedef enum {
    FSOpenReadOnly  = 0x001, ///< Open for reading only
    FSOpenWriteOnly = 0x002, ///< Open for writing only
    FSOpenReadWrite = 0x003, ///< Open for reading and writing
    FSOpenCreate    = 0x100, ///< Create the file if it does not exist
    FSOpenTruncate  = 0x400, ///< Truncate the file to zero length on open
    FSOpenAppend    = 0x800, ///< Move to end of file before every write
} FSOpenFlags;

/// @brief Seek origin, matching POSIX whence constants.
typedef enum {
    FSSeekSet = 0, ///< Offset is from the start of the file
    FSSeekCur = 1, ///< Offset is from the current position
    FSSeekEnd = 2, ///< Offset is from the end of the file
} FSSeekOrigin;

// ============================================================================
// File metadata
// ============================================================================

/// @brief File or directory metadata returned by FileStat().
typedef struct FSStat {
    uint32_t size;  ///< File size in bytes (0 for directories)
    FSMode   mode;  ///< Permission bits
    FSUid    uid;   ///< Owner UID
    bool     isDir; ///< True if this entry is a directory
} FSStat, *FSStatPtr;

#endif // FEATURE_FILE_SYSTEM

#endif // FSTYPES_H
