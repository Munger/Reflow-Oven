/// @file SafeStdLib.h
///
/// @brief Safe wrappers for standard C library functions.
///
/// Each function in this header adds a safety guarantee that the standard
/// equivalent lacks — typically bounded output and guaranteed null-termination.
/// All functions are `static inline` so they carry zero call overhead.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef SAFE_STD_LIB_H
#define SAFE_STD_LIB_H

#include <stddef.h>

// ============================================================================
// String copy  (always null-terminates)
// ============================================================================

/// @brief Bounded string copy — always null-terminates.
/// @param[out] dst  Destination buffer.
/// @param[in]  src  Source string.
/// @param[in]  sz   Size of the destination buffer in bytes.
/// @return The number of bytes copied, excluding the null terminator.
static inline size_t strlcpy_safe( char *restrict dst, const char *restrict src, size_t sz ) {
    if ( sz ) {
        while ( --sz && *src ) *dst++ = *src++;
        *dst = '\0';
    }
    return dst - (char*)0;
}

#endif // SAFE_STD_LIB_H
