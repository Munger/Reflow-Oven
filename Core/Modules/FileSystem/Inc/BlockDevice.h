/// @file BlockDevice.h
///
/// @brief Abstract block device interface for the FileSystem subsystem.
///
/// A block device exposes a uniform read / prog / erase / sync interface over
/// a contiguous range of fixed-size erasable blocks. Concrete implementations
/// (NOR flash, RAM disk, SD card) register a statically allocated BDOps vtable
/// and a private context pointer via BDRegister(). All higher layers — Partition,
/// Volume, and the LittleFS adapter — access storage exclusively through this
/// interface, making the physical medium transparent to them.
///
/// USB Mass Storage can attach directly at this layer, bypassing the filesystem
/// entirely, by calling BDRead/BDProg/BDErase on a raw partition's block range.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef BLOCKDEVICE_H
#define BLOCKDEVICE_H

#include <stdint.h>
#include "FSTypes.h"

// ============================================================================
// Geometry
// ============================================================================

/// @brief Physical geometry of a block device.
typedef struct FSGeometry {
    uint32_t blockSize;  ///< Bytes per erasable block (e.g. 4096)
    uint32_t blockCount; ///< Total number of blocks on the device
    uint32_t readSize;   ///< Minimum readable unit in bytes (typically 1)
    uint32_t progSize;   ///< Minimum programmable unit in bytes (e.g. 256)
} FSGeometry;

// ============================================================================
// Opaque handle
// ============================================================================

/// @brief Opaque handle to a registered block device.
typedef struct FSBlockDevice* BDRef;

// ============================================================================
// Operations vtable
// ============================================================================

/// @brief Block device operations vtable.
///
/// Populate a statically allocated instance of this struct and pass it to
/// BDRegister(). The BDRef is the first argument to every callback so that
/// a single vtable can serve multiple device instances.
typedef struct BDOps {

    /// @brief Read @p size bytes from @p block at byte offset @p off into @p buf.
    ///
    /// @p off and @p size may be any values within [0, blockSize). There are no
    /// alignment requirements on reads for NOR flash.
    FSResult ( *read  )( BDRef bd, uint32_t block, uint32_t off,
                         void* buf, uint32_t size );

    /// @brief Program @p size bytes from @p buf into @p block at byte offset @p off.
    ///
    /// The target range must have been erased to 0xFF. @p off must be aligned
    /// to progSize; @p size must be a multiple of progSize.
    FSResult ( *prog  )( BDRef bd, uint32_t block, uint32_t off,
                         const void* buf, uint32_t size );

    /// @brief Erase @p block entirely, setting all bytes to 0xFF.
    FSResult ( *erase )( BDRef bd, uint32_t block );

    /// @brief Flush any internal cache and confirm the device is idle.
    FSResult ( *sync  )( BDRef bd );

} BDOps;

// ============================================================================
// Public API
// ============================================================================

/// @brief Register a block device and return an opaque handle.
///
/// Stores a reference to @p ops (must remain valid for the lifetime of the
/// block device), the device-private @p context, and the physical @p geometry.
/// Returns NULL if the device pool is full.
///
/// @param ops      Pointer to a statically allocated BDOps vtable.
/// @param context  Device-private state (e.g. a FlashRef).
/// @param geometry Physical geometry of the device.
/// @return Handle, or NULL if the device pool is exhausted.
BDRef BDRegister( const BDOps* ops, void* context, FSGeometry geometry );

/// @brief Return the geometry of a registered block device.
/// @param bd Handle returned by BDRegister().
FSGeometry BDGetGeometry( BDRef bd );

/// @brief Return the device-private context pointer.
/// @param bd Handle returned by BDRegister().
void* BDGetContext( BDRef bd );

FSResult BDRead ( BDRef bd, uint32_t block, uint32_t off,
                  void* buf, uint32_t size );
FSResult BDProg ( BDRef bd, uint32_t block, uint32_t off,
                  const void* buf, uint32_t size );
FSResult BDErase( BDRef bd, uint32_t block );
FSResult BDSync ( BDRef bd );

#endif // BLOCKDEVICE_H
