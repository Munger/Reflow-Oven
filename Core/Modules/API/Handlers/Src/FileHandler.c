/// @file FileHandler.c
///
/// @brief Handler implementations for "/storage/file/" and "/storage/files/" endpoints.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief List a directory under "/storage/files/<path>". @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerFileList( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Read the contents of "/storage/file/<path>". @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerFileRead( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Write raw bytes to "/storage/file/<path>". @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerFileWrite( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Delete "/storage/file/<path>" from storage. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerFileDelete( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
