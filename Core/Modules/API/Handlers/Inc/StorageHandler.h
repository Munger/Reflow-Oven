/// @file StorageHandler.h
///
/// @brief Handler declarations for the /storage (info + format) endpoint group.
///
/// File read/write/list/delete is handled separately in FileHandler.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef STORAGEHANDLER_H
#define STORAGEHANDLER_H

#include "APITypes.h"

/// @brief Return filesystem status and free space.
APIPBPtr HandlerStorageGet( APIPBPtr pb );

/// @brief Format the storage partition.
APIPBPtr HandlerStorageFormat( APIPBPtr pb );

#endif // STORAGEHANDLER_H
