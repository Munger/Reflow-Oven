/// @file StorageHandler.c
///
/// @brief Handler implementations for the /storage (info + format) endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief Return filesystem status and free space. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerStorageGet( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Format the storage partition. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerStorageFormat( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
