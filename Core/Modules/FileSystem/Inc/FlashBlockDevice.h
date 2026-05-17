/// @file FlashBlockDevice.h
///
/// @brief NOR flash concrete block device adapter for the FileSystem subsystem.
///
/// Call FBDRegister() once, after FlashOpen() returns a valid FlashRef, to
/// make the flash chip available to the partition and volume layers.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FLASHBLOCKDEVICE_H
#define FLASHBLOCKDEVICE_H

#include "BlockDevice.h"
#include "Flash.h"

/// @brief Register the NOR flash chip as a block device.
///
/// Wraps the Flash.c read / program / erase / sync functions in a BDOps
/// vtable and derives the geometry from the flash driver's own accessors.
/// The returned BDRef is the entry point for PartInitModule().
///
/// @param flash Handle returned by FlashOpen().
/// @return Block device handle, or NULL if @p flash is NULL or the device
///         pool is full.
BDRef FBDRegister( FlashRef flash );

#endif // FLASHBLOCKDEVICE_H
