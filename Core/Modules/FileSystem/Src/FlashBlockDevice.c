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

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include "FlashBlockDevice.h"
#include "BlockDevice.h"
#include "Flash.h"

// ============================================================================
// BDOps callbacks
// ============================================================================

/// @brief Read from the NOR flash via the block device interface.
/// @param[in]  bd    Block device handle (context is a FlashRef).
/// @param[in]  block Block number relative to the flash start.
/// @param[in]  off   Byte offset within the block.
/// @param[out] buf   Destination buffer.
/// @param[in]  size  Number of bytes to read.
/// @return FSResultOk on success, or FSResultIO on flash error.
static FSResult FBDRead( BDRef bd, uint32_t block, uint32_t off,
                          void* buf, uint32_t size ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash ) + off;
    return FlashRead( flash, addr, buf, size ) ? FSResultOk : FSResultIO;
}

/// @brief Program the NOR flash via the block device interface.
/// @param[in] bd    Block device handle (context is a FlashRef).
/// @param[in] block Block number relative to the flash start.
/// @param[in] off   Byte offset within the block.
/// @param[in] buf   Source data to program.
/// @param[in] size  Number of bytes to program.
/// @return FSResultOk on success, or FSResultIO on flash error.
static FSResult FBDProg( BDRef bd, uint32_t block, uint32_t off,
                          const void* buf, uint32_t size ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash ) + off;
    return FlashProgram( flash, addr, buf, size ) ? FSResultOk : FSResultIO;
}

/// @brief Erase a NOR flash sector via the block device interface.
/// @param[in] bd    Block device handle (context is a FlashRef).
/// @param[in] block Block number to erase (relative to flash start).
/// @return FSResultOk on success, or FSResultIO on flash error.
static FSResult FBDErase( BDRef bd, uint32_t block ) {
    FlashRef flash = (FlashRef)BDGetContext( bd );
    uint32_t addr  = block * FlashGetSectorSize( flash );
    return FlashEraseSector( flash, addr ) ? FSResultOk : FSResultIO;
}

/// @brief Synchronise (flush) the NOR flash via the block device interface.
/// @param[in] bd  Block device handle (context is a FlashRef).
/// @return FSResultOk on success, or FSResultIO on flash error.
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

/// @brief Register the NOR flash chip as a block device.
///
/// Derives FSGeometry from the flash driver and wires the BDOps vtable.
/// Call once after FlashOpen().
///
/// @param[in] flash  FlashRef from FlashOpen().
/// @return BDRef handle, or NULL if flash is NULL.
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

#endif // FEATURE_FILE_SYSTEM
