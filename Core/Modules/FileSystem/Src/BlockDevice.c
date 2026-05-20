/// @file BlockDevice.c
///
/// @brief Abstract block device registry implementation.
///
/// Maintains a fixed-size pool of registered block devices. All operations
/// route through the device's BDOps vtable. The BDRef opaque handle is a
/// direct pointer into the pool, so there is no lookup cost per call.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include "BlockDevice.h"

// ============================================================================
// Private instance type
// ============================================================================

/// @brief Maximum number of simultaneously registered block devices.
enum { kBDMaxDevices = 4 };

typedef struct FSBlockDevice {
    const BDOps* ops;
    void*        context;
    FSGeometry   geometry;
} FSBlockDevice, *FSBlockDevicePtr;

static FSBlockDevice pool[ kBDMaxDevices ];
static uint8_t       count = 0;

// ============================================================================
// Public API
// ============================================================================

/// @brief Register a block device in the shared pool.
/// @param[in] ops      BDOps vtable pointer (must not be NULL).
/// @param[in] context  Opaque context pointer forwarded to vtable callbacks.
/// @param[in] geometry Block device geometry (block size, count, etc.).
/// @return BDRef handle, or NULL if the pool is full or ops is NULL.
BDRef BDRegister( const BDOps* ops, void* context, FSGeometry geometry ) {
    if ( ops == NULL || count >= kBDMaxDevices ) return NULL;
    FSBlockDevicePtr bd = &pool[ count++ ];
    bd->ops      = ops;
    bd->context  = context;
    bd->geometry = geometry;
    return bd;
}

/// @brief Return the geometry of a registered block device.
/// @param[in] bd  Block device handle, or NULL.
/// @return FSGeometry struct (zeroed if bd is NULL).
FSGeometry BDGetGeometry( BDRef bd ) {
    if ( bd == NULL ) return (FSGeometry){ 0 };
    return bd->geometry;
}

/// @brief Return the opaque context pointer for a block device.
/// @param[in] bd  Block device handle, or NULL.
/// @return Context pointer, or NULL if bd is NULL.
void* BDGetContext( BDRef bd ) {
    return bd ? bd->context : NULL;
}

/// @brief Read from a block device at block + offset.
/// @param[in]  bd    Block device handle.
/// @param[in]  block Block number.
/// @param[in]  off   Byte offset within the block.
/// @param[out] buf   Destination buffer.
/// @param[in]  size  Number of bytes to read.
/// @return FSResultOk on success, or an FSResult error code.
FSResult BDRead( BDRef bd, uint32_t block, uint32_t off,
                 void* buf, uint32_t size ) {
    if ( bd == NULL || bd->ops->read == NULL ) return FSResultNotReady;
    return bd->ops->read( bd, block, off, buf, size );
}

/// @brief Program (write) a block device at block + offset.
/// @param[in] bd    Block device handle.
/// @param[in] block Block number.
/// @param[in] off   Byte offset within the block.
/// @param[in] buf   Source data to write.
/// @param[in] size  Number of bytes to program.
/// @return FSResultOk on success, or an FSResult error code.
FSResult BDProg( BDRef bd, uint32_t block, uint32_t off,
                 const void* buf, uint32_t size ) {
    if ( bd == NULL || bd->ops->prog == NULL ) return FSResultNotReady;
    return bd->ops->prog( bd, block, off, buf, size );
}

/// @brief Erase a single block on the device.
/// @param[in] bd    Block device handle.
/// @param[in] block Block number to erase.
/// @return FSResultOk on success, or an FSResult error code.
FSResult BDErase( BDRef bd, uint32_t block ) {
    if ( bd == NULL || bd->ops->erase == NULL ) return FSResultNotReady;
    return bd->ops->erase( bd, block );
}

/// @brief Synchronise (flush) any pending writes on a block device.
/// @param[in] bd  Block device handle.
/// @return FSResultOk on success, or an FSResult error code.
FSResult BDSync( BDRef bd ) {
    if ( bd == NULL || bd->ops->sync == NULL ) return FSResultNotReady;
    return bd->ops->sync( bd );
}

#endif // FEATURE_FILE_SYSTEM
