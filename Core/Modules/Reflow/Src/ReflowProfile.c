/// @file ReflowProfile.c
///
/// @brief Reflow profile data structures and filesystem I/O — implementation.
///
/// Profiles are stored on flash as a fixed-size binary blob: a 8-byte header
/// (magic + version) followed by a ReflowProfile struct. Bare filenames are
/// resolved to REFLOW_PROFILE_DIR. All filesystem access is synchronous and
/// uses UID 0 (system context) so no permission negotiation is needed.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "ReflowProfile.h"
#include "FSFile.h"

// ============================================================================
// On-disk format
// ============================================================================

#define PROFILE_MAGIC   0x52464C50UL  ///< "PLFR" little-endian, identifies a reflow profile file
#define PROFILE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  reserved[ 3 ];
} ProfileFileHeader;

// ============================================================================
// Hardcoded default profile
// ============================================================================

/// @brief Standard Pb-free (SAC305) reflow profile for 0.8 mm FR4.
///
/// Stage 0 — Preheat:  ramp to 150 °C at 2 °C/s; hold 60 s; all heaters; fan 20 %.
/// Stage 1 — Soak:     ramp to 183 °C at 0.8 °C/s; hold 90 s; all heaters; fan 15 %.
/// Stage 2 — Reflow:   ramp to 245 °C at 3 °C/s; hold 45 s above liquidus; fan off.
/// Stage 3 — Cooldown: set target to 50 °C; hold until temperature drops to 50 °C; fan 80 %.
static const ReflowProfile kDefaultProfile = {
    .name       = "Default Pb-Free",
    .stageCount = 4,
    .stages = {
        {   // Stage 0 — Preheat
            .type          = ReflowStagePreheat,
            .targetTempC   = 150,
            .rampRateC     = 2.0f,
            .holdCriterion = ReflowHoldTime,
            .holdMs        = 60000,
            .fanStart      = 200,
            .fanEnd        = 200,
            .heaters       = 0x07,
        },
        {   // Stage 1 — Soak
            .type          = ReflowStageSoak,
            .targetTempC   = 183,
            .rampRateC     = 0.8f,
            .holdCriterion = ReflowHoldTime,
            .holdMs        = 90000,
            .fanStart      = 150,
            .fanEnd        = 150,
            .heaters       = 0x07,
        },
        {   // Stage 2 — Reflow
            .type          = ReflowStageReflow,
            .targetTempC   = 245,
            .rampRateC     = 3.0f,
            .holdCriterion = ReflowHoldTime,
            .holdMs        = 45000,
            .fanStart      = 0,
            .fanEnd        = 0,
            .heaters       = 0x07,
        },
        {   // Stage 3 — Cooldown
            .type          = ReflowStageCool,
            .targetTempC   = 50,
            .rampRateC     = -3.0f,
            .holdCriterion = ReflowHoldTemperature,
            .holdTempC     = 50,
            .fanStart      = 800,
            .fanEnd        = 800,
            .heaters       = 0x00,
        },
    },
};

// ============================================================================
// Internal helpers
// ============================================================================

/// @brief Resolve @p name to a full absolute path in @p dst.
///
/// If @p name already starts with '/', it is used verbatim. Otherwise the
/// REFLOW_PROFILE_DIR prefix is prepended. @p dst is always NUL-terminated.
static void BuildPath( char* dst, size_t dstSize, const char* name ) {
    if ( name[ 0 ] == '/' ) {
        size_t len = strlen( name );
        if ( len + 1 <= dstSize ) {
            memcpy( dst, name, len + 1 );
        } else {
            dst[ 0 ] = '\0';
        }
    } else {
        const char prefix[] = REFLOW_PROFILE_DIR "/";
        size_t     prefixLen = sizeof( prefix ) - 1;
        size_t     nameLen   = strlen( name );
        if ( prefixLen + nameLen + 1 <= dstSize ) {
            memcpy( dst, prefix, prefixLen );
            memcpy( dst + prefixLen, name, nameLen + 1 );
        } else {
            dst[ 0 ] = '\0';
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Return a pointer to the hardcoded default Pb-free reflow profile.
///
/// The returned pointer is valid for the lifetime of the program.
const ReflowProfile* ReflowProfileGetDefault( void ) {
    return &kDefaultProfile;
}

/// @brief Load a reflow profile from the filesystem into @p out.
///
/// The file is read as a ProfileFileHeader followed by a ReflowProfile struct.
/// Returns false if the file is absent, the header magic or version is wrong,
/// the read size is incorrect, or stageCount exceeds kReflowMaxStages.
bool ReflowProfileLoad( const char* name, ReflowProfilePtr out ) {
    if ( !name || !out ) return false;

    char path[ 128 ];
    BuildPath( path, sizeof( path ), name );

    FileRef f = FileOpen( path, FSOpenReadOnly, 0 );
    if ( !f ) return false;

    ProfileFileHeader hdr;
    size_t            read = 0;

    bool ok = ( FileRead( f, &hdr, sizeof( hdr ), &read ) == FSResultOk ) &&
              ( read      == sizeof( hdr )     ) &&
              ( hdr.magic   == PROFILE_MAGIC   ) &&
              ( hdr.version == PROFILE_VERSION );

    if ( ok ) {
        ok = ( FileRead( f, out, sizeof( ReflowProfile ), &read ) == FSResultOk ) &&
             ( read == sizeof( ReflowProfile ) ) &&
             ( out->stageCount <= kReflowMaxStages );
    }

    FileClose( f );
    return ok;
}

/// @brief Save a reflow profile to the filesystem.
///
/// Writes a ProfileFileHeader followed by the ReflowProfile struct. Creates
/// the file if absent; truncates it if it exists. Returns false on any I/O
/// error or if path resolution fails.
bool ReflowProfileSave( const ReflowProfile* profile, const char* name ) {
    if ( !profile || !name ) return false;

    char path[ 128 ];
    BuildPath( path, sizeof( path ), name );
    if ( path[ 0 ] == '\0' ) return false;

    FileRef f = FileOpen( path, FSOpenWriteOnly | FSOpenCreate | FSOpenTruncate, 0 );
    if ( !f ) return false;

    ProfileFileHeader hdr = { .magic = PROFILE_MAGIC, .version = PROFILE_VERSION };
    size_t            written = 0;

    bool ok = ( FileWrite( f, &hdr, sizeof( hdr ), &written ) == FSResultOk ) &&
              ( written == sizeof( hdr ) );

    if ( ok ) {
        ok = ( FileWrite( f, profile, sizeof( ReflowProfile ), &written ) == FSResultOk ) &&
             ( written == sizeof( ReflowProfile ) );
    }

    FileClose( f );
    return ok;
}
