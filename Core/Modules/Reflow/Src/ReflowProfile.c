/// @file ReflowProfile.c
///
/// @brief Reflow profile — heap-allocated linked list and tagged-CSV filesystem I/O.
///
/// ReflowProfileLoad() allocates the profile struct and each stage node from
/// the FreeRTOS heap with pvPortMalloc(). ReflowProfileFree() walks the list
/// and releases every node, then the profile itself. The built-in default is
/// described by a static template array; BuildDefault() copies it to the heap
/// so the caller always owns the result under the same contract.
///
/// File format: plain ASCII tagged CSV, endian-agnostic, CLI-transferable.
/// Lines starting with '#' and blank lines are ignored. Every other line is a
/// stage record — a comma-separated list of 2-char tag+value tokens. Tags may
/// appear in any order; unrecognised tags are silently ignored. The profile
/// name is taken from the filename.
///
///   ty<str>   Stage label (optional, no spaces).
///   tc<int>   Target temperature in milli-°C.
///   rr<int>   Ramp rate in milli-°C/s, signed.
///   to<int>   Timeout ms waiting for target temperature. 0 = no timeout.
///   hv<int>   Hold duration in ms once target is reached.
///   fs<int>   Fan speed, permille.
///   fa<int>   Fan acceleration time in ms. 0 = instant.
///   h0<int>   Heater top, 0/1.
///   h1<int>   Heater rear, 0/1.
///   h2<int>   Heater bottom, 0/1.
///   fn<int>   Action at hold entry (ReflowFn enum). 0 = none.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

#include "ReflowProfile.h"
#include "FSFile.h"
#include "FSUtils.h"

// ============================================================================
// Format constants
// ============================================================================

enum {
    kFileBufSize    = 4096,
    kLineBufSize    = 128,
    kFieldDelimiter = ',',
};


// ============================================================================
// Tag system
//
// Each comma-separated token "xxvalue" is passed whole to ParseToken(), which
// matches on the 2-char key then dispatches by FieldID. Adding a new field:
// one row in kTagTable, one case in ParseToken.
// ============================================================================

typedef enum {
    kFieldStageType,     ///< ty — optional stage label.
    kFieldTimeoutMs,     ///< to — maximum ms to wait for target temperature; 0 = no timeout.
    kFieldHoldMs,        ///< hv — hold duration in ms once target temperature is reached.
    kFieldTargetTemp,
    kFieldRampRate,
    kFieldFanSpeed,
    kFieldAccelTimeMs,
    kFieldHeaterTop,
    kFieldHeaterRear,
    kFieldHeaterBottom,
    kFieldFunction,
} FieldID;

// clang-format off
static const struct {
    const char* fmt;
    FieldID     fieldID;
} kTagTable[] = {
    { "ty%s",    kFieldStageType    },
    { "to%u",    kFieldTimeoutMs    },
    { "hv%u",    kFieldHoldMs       },
    { "tc%d",    kFieldTargetTemp   },
    { "rr%d",    kFieldRampRate     },
    { "fs%hu",   kFieldFanSpeed     },
    { "fa%u",    kFieldAccelTimeMs  },
    { "h0%hhd",  kFieldHeaterTop    },
    { "h1%hhd",  kFieldHeaterRear   },
    { "h2%hhd",  kFieldHeaterBottom },
    { "fn%d",    kFieldFunction     },
};
// clang-format on
enum { kTagTableLen = sizeof( kTagTable ) / sizeof( kTagTable[0] ) };

static void ParseToken( const char* token, ReflowStagePtr s ) {
    for ( int i = 0; i < kTagTableLen; i++ ) {
        const char* fmt = kTagTable[i].fmt;
        if ( token[0] != fmt[0] || token[1] != fmt[1] ) continue;
        switch ( kTagTable[i].fieldID ) {
            case kFieldStageType:    sscanf( token, fmt, s->name        ); break;
            case kFieldTimeoutMs:    sscanf( token, fmt, &s->timeoutMs  ); break;
            case kFieldHoldMs:       sscanf( token, fmt, &s->holdMs     ); break;
            case kFieldTargetTemp:   sscanf( token, fmt, &s->targetTemp ); break;
            case kFieldRampRate:     sscanf( token, fmt, &s->rampRate   ); break;
            case kFieldFanSpeed:     sscanf( token, fmt, &s->fanSpeed   ); break;
            case kFieldAccelTimeMs:  sscanf( token, fmt, &s->accelTimeMs); break;
            case kFieldHeaterTop:    sscanf( token, fmt, &s->heaterTop  ); break;
            case kFieldHeaterRear:   sscanf( token, fmt, &s->heaterRear ); break;
            case kFieldHeaterBottom: sscanf( token, fmt, &s->heaterBottom); break;
            case kFieldFunction:     sscanf( token, fmt, &s->function   ); break;
        }
        return;
    }
}


/// @brief Allocate a stage node, fill it from the comma-separated tokens in @p line,
/// and link it at @p tail.
static void ParseLine( const char* line, ReflowStagePtr* tail ) {
    static char buf[ kLineBufSize ];
    strncpy( buf, line, sizeof( buf ) - 1 );
    buf[ sizeof( buf ) - 1 ] = '\0';

    ReflowStagePtr s = pvPortMalloc( sizeof( ReflowStage ) );
    if ( !s ) return;
    memset( s, 0, sizeof( ReflowStage ) );

    char* p = buf;
    while ( *p ) {
        char* comma = strchr( p, kFieldDelimiter );
        char* next  = comma ? comma + 1 : p + strlen( p );
        if ( comma ) *comma = '\0';
        ParseToken( p, s );
        p = next;
    }

    *tail = s;
}

// ============================================================================
// Built-in default profile
// ============================================================================

/// @brief Template data for the built-in SAC305 (Pb-free) profile.
///
/// All temperatures in milli-°C; ramp rates in milli-°C/s.
/// BuildDefault() copies these to the heap so the caller always owns the result
/// under the same contract as a file-loaded profile.
static const ReflowStage kDefaultStages[] = {
    {   // Preheat: ramp to 150 °C at 2 °C/s, hold 60 s, all heaters, fan 20 %
        .name         = "Preheat",
        .targetTemp   = 150000,
        .rampRate     = 2000,
        .holdMs       = 60000,
        .fanSpeed     = 200,
        .heaterTop    = true,
        .heaterRear   = true,
        .heaterBottom = true,
    },
    {   // Soak: ramp to 183 °C at 0.8 °C/s, hold 90 s, all heaters, fan 15 %
        .name         = "Soak",
        .targetTemp   = 183000,
        .rampRate     = 800,
        .holdMs       = 90000,
        .fanSpeed     = 150,
        .heaterTop    = true,
        .heaterRear   = true,
        .heaterBottom = true,
    },
    {   // Reflow: ramp to 245 °C at 3 °C/s, hold 45 s, all heaters, fan off
        .name         = "Reflow",
        .targetTemp   = 245000,
        .rampRate     = 3000,
        .holdMs       = 45000,
        .fanSpeed     = 0,
        .heaterTop    = true,
        .heaterRear   = true,
        .heaterBottom = true,
    },
    {   // Cooldown: ramp to 50 °C at -3 °C/s, hold 10 s, fan 80 %, no heaters
        .name         = "Cooldown",
        .targetTemp   = 50000,
        .rampRate     = -3000,
        .holdMs       = 10000,
        .fanSpeed     = 800,
        .heaterTop    = false,
        .heaterRear   = false,
        .heaterBottom = false,
    },
};
enum { kDefaultStageCount = sizeof( kDefaultStages ) / sizeof( kDefaultStages[0] ) };

/// @brief Heap-allocate a copy of the built-in default profile.
static ReflowProfilePtr BuildDefault( void ) {
    ReflowProfilePtr p = pvPortMalloc( sizeof( ReflowProfile ) );
    if ( !p ) return NULL;

    p->stages = NULL;

    ReflowStagePtr* tail = &p->stages;
    for ( int i = 0; i < kDefaultStageCount; i++ ) {
        ReflowStagePtr node = pvPortMalloc( sizeof( ReflowStage ) );
        if ( !node ) { ReflowProfileFree( p ); return NULL; }
        *node      = kDefaultStages[i];
        node->next = NULL;
        *tail      = node;
        tail       = &node->next;
    }

    return p;
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Load a named profile from flash, or return the built-in default.
ReflowProfilePtr ReflowProfileLoad( const char* name ) {
#if FEATURE_FILE_SYSTEM
    if ( name ) {
        static char path[ kFSMaxPathLen + 1 ];
        FSPathBuild( path, sizeof( path ), REFLOW_PROFILE_DIR, name );

        FileRef f = FileOpen( path, FSOpenReadOnly, 0 );
        if ( f ) {
            static char buf[ kFileBufSize ];
            size_t bytesRead = 0;
            bool   readOk = ( FileRead( f, buf, sizeof( buf ) - 1, &bytesRead ) == FSResultOk );
            FileClose( f );

            if ( readOk && bytesRead > 0 ) {
                buf[ bytesRead ] = '\0';

                ReflowProfilePtr out = pvPortMalloc( sizeof( ReflowProfile ) );
                if ( !out ) return NULL;
                out->stages = NULL;

                ReflowStagePtr* tailPtr = &out->stages;

                char* p = buf;
                while ( *p ) {
                    char* nl   = strchr( p, '\n' );
                    char* next = nl ? nl + 1 : p + strlen( p );
                    if ( nl ) *nl = '\0';

                    size_t len = strlen( p );
                    if ( len > 0 && p[ len - 1 ] == '\r' ) p[ len - 1 ] = '\0';

                    if ( p[0] != '#' && p[0] != '\0' ) {
                        ParseLine( p, tailPtr );
                        while ( *tailPtr ) tailPtr = &(*tailPtr)->next;
                    }

                    p = next;
                }

                if ( out->stages ) return out;
                ReflowProfileFree( out );
            }
        }
    }
#else
    (void)name;
#endif // FEATURE_FILE_SYSTEM

    return BuildDefault();
}

/// @brief Walk the stage list and free every node, then the profile itself.
void ReflowProfileFree( ReflowProfilePtr profile ) {
    if ( !profile ) return;
    ReflowStagePtr s = profile->stages;
    while ( s ) {
        ReflowStagePtr next = s->next;
        vPortFree( s );
        s = next;
    }
    vPortFree( profile );
}

/// @brief Serialise a profile to tagged CSV and write it to flash.
bool ReflowProfileSave( ReflowProfilePtr profile, const char* name ) {
    if ( !profile || !name || !profile->stages ) return false;

    static char path[ kFSMaxPathLen + 1 ];
    FSPathBuild( path, sizeof( path ), REFLOW_PROFILE_DIR, name );
    if ( path[0] == '\0' ) return false;

    static char buf[ kFileBufSize ];
    size_t pos = 0;
    size_t rem = sizeof( buf );

#define APPEND( ... ) do { \
    int _n = snprintf( buf + pos, rem, __VA_ARGS__ ); \
    if ( _n < 0 || (size_t)_n >= rem ) return false; \
    pos += (size_t)_n; rem -= (size_t)_n; \
} while ( 0 )

    for ( ReflowStagePtr s = profile->stages; s; s = s->next ) {
        if ( s->name[0] ) {
            APPEND( "ty%s,tc%d,rr%d,to%u,hv%u,fs%hu,fa%u,h0%u,h1%u,h2%u,fn%d\n",
                    s->name, (int)s->targetTemp, (int)s->rampRate,
                    (unsigned)s->timeoutMs, (unsigned)s->holdMs, s->fanSpeed, (unsigned)s->accelTimeMs,
                    (unsigned)s->heaterTop, (unsigned)s->heaterRear, (unsigned)s->heaterBottom,
                    (int)s->function );
        } else {
            APPEND( "tc%d,rr%d,to%u,hv%u,fs%hu,fa%u,h0%u,h1%u,h2%u,fn%d\n",
                    (int)s->targetTemp, (int)s->rampRate,
                    (unsigned)s->timeoutMs, (unsigned)s->holdMs, s->fanSpeed, (unsigned)s->accelTimeMs,
                    (unsigned)s->heaterTop, (unsigned)s->heaterRear, (unsigned)s->heaterBottom,
                    (int)s->function );
        }
    }

#undef APPEND

    FSEnsureDir( REFLOW_PROFILE_DIR, 0 );
    FileRef f = FileOpen( path, FSOpenWriteOnly | FSOpenCreate | FSOpenTruncate, 0 );
    if ( !f ) return false;

    size_t written = 0;
    bool ok = ( FileWrite( f, buf, pos, &written ) == FSResultOk ) && ( written == pos );
    FileClose( f );
    return ok;
}
