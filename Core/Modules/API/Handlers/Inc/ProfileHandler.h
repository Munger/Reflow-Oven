/// @file ProfileHandler.h
///
/// @brief Handler declarations for the /profiles endpoint group.
///
/// Provides full CRUD over stored reflow profiles. Profiles are
/// serialised as tagged CSV on the wire and stored on the internal
/// filesystem.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef PROFILEHANDLER_H
#define PROFILEHANDLER_H

#include "APITypes.h"

/// @brief Return a list of all stored profile names.
APIPBPtr HandlerProfileList( APIPBPtr pb );

/// @brief Return a single profile by name, serialised as CSV.
APIPBPtr HandlerProfileGet( APIPBPtr pb );

/// @brief Create a new profile from a CSV body.
APIPBPtr HandlerProfileCreate( APIPBPtr pb );

/// @brief Replace an existing profile with a new CSV body.
APIPBPtr HandlerProfileUpdate( APIPBPtr pb );

/// @brief Delete a named profile from storage.
APIPBPtr HandlerProfileDelete( APIPBPtr pb );

#endif // PROFILEHANDLER_H
