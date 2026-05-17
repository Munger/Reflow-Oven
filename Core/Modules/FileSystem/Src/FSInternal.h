/// @file FSInternal.h
///
/// @brief Package-private declarations shared within the FileSystem module.
///
/// This header is intentionally kept in Src/ rather than Inc/ so it is
/// not visible to code outside the FileSystem module. It exposes:
///   - The LittleFS error-to-FSResult mapping function
///   - The package-private VolGetLFS() accessor needed by FSFile.c
///
/// Do NOT include this header from outside Core/Modules/FileSystem/Src/.

#ifndef FSINTERNAL_H
#define FSINTERNAL_H

#include "lfs.h"
#include "FSTypes.h"
#include "Volume.h"

/// @brief Map a LittleFS negative error code to the equivalent FSResult.
FSResult FSMapLFSError( int lfsErr );

/// @brief Return the lfs_t instance for a mounted volume.
///
/// Used by FSFile.c to call lfs_file_* functions without exposing the
/// lfs_t type in the public Volume.h header.
///
/// @param vol Handle returned by VolMount().
/// @return Pointer to the internal lfs_t, or NULL if vol is NULL or unmounted.
lfs_t* VolGetLFS( VolRef vol );

/// @brief Return the open-file reference count for a volume.
///
/// Used by VolUnmount() to refuse to unmount while files are still open.
/// Maintained exclusively by FSFile.c.
uint8_t VolGetOpenFileCount( VolRef vol );

/// @brief Increment the open-file reference count for a volume.
void VolIncrementOpenFiles( VolRef vol );

/// @brief Decrement the open-file reference count for a volume.
void VolDecrementOpenFiles( VolRef vol );

#endif // FSINTERNAL_H
