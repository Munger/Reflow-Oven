/// @file ProfileHandler.c
///
/// @brief Handler implementations for the /profiles endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "APIHandlers.h"

/// @brief Return a list of all stored profile names. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerProfileList( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Return a single profile by name, serialised as CSV. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerProfileGet( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Create a new profile from a CSV body. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerProfileCreate( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Replace an existing profile with a new CSV body. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerProfileUpdate( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}

/// @brief Delete a named profile from storage. @param[in] pb Request PB owned by caller. @return Response PB or NULL.
APIPBPtr HandlerProfileDelete( APIPBPtr pb ) {
    UNUSED( pb );
    return NULL;
}
