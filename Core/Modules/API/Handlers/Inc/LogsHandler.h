/// @file LogsHandler.h
///
/// @brief Handler declarations for the /logs endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef LOGSHANDLER_H
#define LOGSHANDLER_H

#include "APITypes.h"

/// @brief List all stored log files.
APIPBPtr HandlerLogsList( APIPBPtr pb );

/// @brief Return the contents of a single log file.
APIPBPtr HandlerLogGet( APIPBPtr pb );

/// @brief Delete a single log file by name.
APIPBPtr HandlerLogDelete( APIPBPtr pb );

/// @brief Delete all stored log files.
APIPBPtr HandlerLogsClear( APIPBPtr pb );

#endif // LOGSHANDLER_H
