/// @file FileHandler.h
///
/// @brief Handler declarations for "/storage/file/" and "/storage/files/" endpoints.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include "APITypes.h"

/// @brief List a directory under "/storage/files/<path>".
APIPBPtr HandlerFileList( APIPBPtr pb );

/// @brief Read the contents of "/storage/file/<path>".
APIPBPtr HandlerFileRead( APIPBPtr pb );

/// @brief Write raw bytes to "/storage/file/<path>".
APIPBPtr HandlerFileWrite( APIPBPtr pb );

/// @brief Delete "/storage/file/<path>" from storage.
APIPBPtr HandlerFileDelete( APIPBPtr pb );

#endif // FILEHANDLER_H
