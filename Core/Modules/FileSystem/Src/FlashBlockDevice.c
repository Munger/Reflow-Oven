/// @file FlashBlockDevice.c
///
/// @brief Registers the NOR flash driver (Flash.c) as a FileSystem block device.
///
/// Provides the BDOps vtable that wraps FlashRead / FlashProgram /
/// FlashEraseSector / FlashSync and derives the FSGeometry from the flash
/// driver's own geometry accessors. Call FBDRegister() once after FlashOpen()
/// to make the flash chip available to the partition and volume layers.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "FlashBlockDevice.h"
#include "BlockDevice.h"
#include "Flash.h"

// ============================================================================
// BDOps callbacks
// ============================================================================

static FSResult FBDRead( BDRef bd, uint32_t block, uint32_t off,
                          void* buf, uint32_t size ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash ) + off;
    return FlashRead( flash, addr, buf, size ) ? FSResultOk : FSResultIO;
}

static FSResult FBDProg( BDRef bd, uint32_t block, uint32_t off,
                          const void* buf, uint32_t size ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash ) + off;
    return FlashProgram( flash, addr, buf, size ) ? FSResultOk : FSResultIO;
}

static FSResult FBDErase( BDRef bd, uint32_t block ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash );
    return FlashEraseSector( flash, addr ) ? FSResultOk : FSResultIO;
}

static FSResult FBDSync( BDRef bd ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    return FlashSync( flash ) ? FSResultOk : FSResultIO;
}

static const BDOps kFlashOps = {
    .read  = FBDRead,
    .prog  = FBDProg,
    .erase = FBDErase,
    .sync  = FBDSync,
};

// ============================================================================
// Public API
// ============================================================================

BDRef FBDRegister( FlashRef flash ) {
    if ( flash == NULL ) return NULL;
    FSGeometry geo = {
        .blockSize  = FlashGetSectorSize( flash ),
        .blockCount = FlashGetSectorCount( flash ),
        .readSize   = 1,
        .progSize   = FlashGetPageSize( flash ),
    };
    return BDRegister( &kFlashOps, flash, geo );
}
