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

BDRef BDRegister( const BDOps* ops, void* context, FSGeometry geometry ) {
    if ( ops == NULL || count >= kBDMaxDevices ) return NULL;
    FSBlockDevicePtr bd = &pool[ count++ ];
    bd->ops      = ops;
    bd->context  = context;
    bd->geometry = geometry;
    return bd;
}

FSGeometry BDGetGeometry( BDRef bd ) {
    if ( bd == NULL ) return (FSGeometry){ 0 };
    return bd->geometry;
}

void* BDGetContext( BDRef bd ) {
    return bd ? bd->context : NULL;
}

FSResult BDRead( BDRef bd, uint32_t block, uint32_t off,
                 void* buf, uint32_t size ) {
    if ( bd == NULL || bd->ops->read == NULL ) return FSResultNotReady;
    return bd->ops->read( bd, block, off, buf, size );
}

FSResult BDProg( BDRef bd, uint32_t block, uint32_t off,
                 const void* buf, uint32_t size ) {
    if ( bd == NULL || bd->ops->prog == NULL ) return FSResultNotReady;
    return bd->ops->prog( bd, block, off, buf, size );
}

FSResult BDErase( BDRef bd, uint32_t block ) {
    if ( bd == NULL || bd->ops->erase == NULL ) return FSResultNotReady;
    return bd->ops->erase( bd, block );
}

FSResult BDSync( BDRef bd ) {
    if ( bd == NULL || bd->ops->sync == NULL ) return FSResultNotReady;
    return bd->ops->sync( bd );
}
