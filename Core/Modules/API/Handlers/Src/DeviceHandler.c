/// @file DeviceHandler.c
///
/// @brief Handler implementations for the /devices endpoint group.
///
/// Dispatches to per-type driver logic based on the type string extracted
/// from the request path. Each type handler checks its compile-time feature
/// gate and returns APIStatusNotImplemented (501) when the hardware is not
/// fitted on this board revision.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <stdio.h>
#include <string.h>

#include "APIHandlers.h"
#include "DriverRegistry.h"
#include "APIRoutes.h"
#include "APICore.h"
#include "Features.h"
#include "Triac.h"
#include "Thermocouple.h"
#include "Thermistor.h"
#include "ThermistorI2C.h"
#include "ACFan.h"
#include "ACLight.h"
#include "DCFan.h"
#include "Buzzer.h"
#include "OvenController.h"

// ============================================================================
// Types
// ============================================================================

typedef struct DeviceTypeEntry {
    const char* type;
    APIPBPtr ( *get )( APIPBPtr, const char* );
    APIPBPtr ( *set )( APIPBPtr, const char* );
} DeviceTypeEntry;

typedef const DeviceTypeEntry* DeviceTypeEntryPtr;

// ============================================================================
// Forward declarations
// ============================================================================

static bool                 AppendRaw( APIPBPtr, const char* );
static void                 IntToDec( char*, size_t, int32_t );
static bool                 AppendIntField( APIPBPtr, const char*, int32_t );
static bool                 AppendBoolField( APIPBPtr, const char*, int );
static APIPBPtr             HandleHeaterGet( APIPBPtr, const char* );
static APIPBPtr             HandleHeaterSet( APIPBPtr, const char* );
static APIPBPtr             HandleThermocoupleGet( APIPBPtr, const char* );
static APIPBPtr             HandleThermistorGet( APIPBPtr, const char* );
static APIPBPtr             HandleTachometerGet( APIPBPtr, const char* );
static APIPBPtr             HandleFanGet( APIPBPtr, const char* );
static APIPBPtr             HandleFanSet( APIPBPtr, const char* );
static APIPBPtr             HandleLightGet( APIPBPtr, const char* );
static APIPBPtr             HandleLightSet( APIPBPtr, const char* );
static APIPBPtr             HandleOvenGet( APIPBPtr, const char* );

// ============================================================================
// Device type dispatch table
// ============================================================================

static const DeviceTypeEntry deviceTypes[] = {
    { "heater",       HandleHeaterGet,       HandleHeaterSet       },
    { "thermocouple", HandleThermocoupleGet, NULL                  },
    { "thermistor",   HandleThermistorGet,   NULL                  },
    { "tachometer",   HandleTachometerGet,   NULL                  },
    { "fan",          HandleFanGet,          HandleFanSet          },
    { "light",        HandleLightGet,        HandleLightSet        },
    { "oven",         HandleOvenGet,         NULL                  },
};

enum { kDeviceTypeCount = sizeof( deviceTypes ) / sizeof( deviceTypes[ 0 ] ) };

// ============================================================================
// Payload append helpers  (no printf — zero-weight integer conversion)
// ============================================================================

static bool AppendRaw( APIPBPtr pb, const char* s ) {
    size_t slen = strlen( s );
    PayloadPtr* tail = &pb->payload;
    while ( *tail ) tail = &( *tail )->next;

    PayloadPtr node = *tail;
    size_t off = node ? strlen( node->data ) : 0;

    while ( slen > 0 ) {
        if ( !node || off >= kApiPayloadSize ) {
            node = AcquirePayload();
            if ( !node ) return false;
            node->next = NULL;
            node->data[ 0 ] = '\0';
            *tail = node;
            off = 0;
        }
        size_t room = kApiPayloadSize - 1 - off;
        size_t copy = ( slen < room ) ? slen : room;
        memcpy( node->data + off, s, copy );
        off += copy;
        s   += copy;
        slen -= copy;
        node->data[ off ] = '\0';
    }
    return true;
}

static void IntToDec( char* buf, size_t size, int32_t val ) {
    char tmp[ 16 ];
    int  pos = sizeof( tmp );
    tmp[ --pos ] = '\0';
    uint32_t u = ( val < 0 ) ? (uint32_t)( -val ) : (uint32_t)val;
    do {
        tmp[ --pos ] = (char)( '0' + ( u % 10U ) );
        u /= 10U;
    } while ( u > 0 );
    if ( val < 0 ) tmp[ --pos ] = '-';
    size_t len = sizeof( tmp ) - pos;
    if ( len >= size ) len = size - 1;
    memcpy( buf, tmp + pos, len );
    buf[ len ] = '\0';
}

static bool AppendIntField( APIPBPtr pb, const char* key, int32_t val ) {
    static char buf[ 64 ];
    size_t pos = 0;
    buf[ pos++ ] = '"';
    while ( *key && pos < sizeof( buf ) - 1 ) buf[ pos++ ] = *key++;
    buf[ pos++ ] = '"';
    buf[ pos++ ] = ':';
    buf[ pos++ ] = ' ';
    char valStr[ 16 ];
    IntToDec( valStr, sizeof( valStr ), val );
    size_t vi = 0;
    while ( valStr[ vi ] && pos < sizeof( buf ) - 1 ) buf[ pos++ ] = valStr[ vi++ ];
    buf[ pos ] = '\0';
    return AppendRaw( pb, buf );
}

static bool AppendBoolField( APIPBPtr pb, const char* key, int v ) {
    static char buf[ 64 ];
    size_t pos = 0;
    buf[ pos++ ] = '"';
    while ( *key && pos < sizeof( buf ) - 1 ) buf[ pos++ ] = *key++;
    buf[ pos++ ] = '"';
    buf[ pos++ ] = ':';
    buf[ pos++ ] = ' ';
    const char* valStr = v ? "true" : "false";
    while ( *valStr && pos < sizeof( buf ) - 1 ) buf[ pos++ ] = *valStr++;
    buf[ pos ] = '\0';
    return AppendRaw( pb, buf );
}

// ============================================================================
// Feature gate by ID  (strcmp-free alternative to HeaterFeature)
// ============================================================================

static int HeaterFeatureByID( TriacID id ) {
    switch ( id ) {
        case TriacHeaterTop:    return FEATURE_HEATER_TOP;
        case TriacHeaterRear:   return FEATURE_HEATER_REAR;
        case TriacHeaterBottom: return FEATURE_HEATER_BOTTOM;
        default:                return 0;
    }
}

// ============================================================================
// Heater
// ============================================================================

static APIPBPtr HandleHeaterGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "triac", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
        TriacID id = (TriacID)raw;
        if ( !HeaterFeatureByID( id ) ) {
            resp = APIResponseStatus( pb, APIStatusNotImplemented );
        } else {
            TriacRef ref = TriacOpen( id );
            if ( !ref ) {
                resp = APIResponseStatus( pb, APIStatusUnavailable );
            } else {
                uint32_t st = TriacGetStatus( ref );
                int on    = ( st & BIT( FlagTriacStatusActive ) ) ? 1 : 0;
                int ready = ( st & BIT( FlagTriacStatusReady ) ) ? 1 : 0;

                resp = AcquirePB();
                if ( resp ) {
                    resp->status     = APIStatusOK;
                    resp->syntax     = pb->syntax;
                    resp->terminator = pb->terminator;
                    AppendRaw( resp, "{ " );
                    AppendIntField( resp, "power", on ? 100 : 0 );
                    AppendRaw( resp, ", " );
                    AppendBoolField( resp, "on", on );
                    AppendRaw( resp, ", " );
                    AppendBoolField( resp, "ready", ready );
                    AppendRaw( resp, " }" );
                }
            }
        }
    }

    return resp;
}

static APIPBPtr HandleHeaterSet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "triac", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
        TriacID id = (TriacID)raw;
        if ( !HeaterFeatureByID( id ) ) {
            resp = APIResponseStatus( pb, APIStatusNotImplemented );
        } else {
            // TODO: TriacOpen( id ), parse power (0–100) from request body, convert to permille (×10), call Triac drive
            resp = APIResponseStatus( pb, APIStatusOK );
        }
    }

    return resp;
}

// ============================================================================
// Thermocouple
// ============================================================================

static APIPBPtr HandleThermocoupleGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

#if !FEATURE_THERMOCOUPLES
    resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
    uint16_t raw = DriverFindInstance( "tc", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
        ThermocoupleRef ref = TCOpen( (ThermocoupleID)raw );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            int32_t temp = TCGetTemperature( ref );
            int32_t cjt  = TCGetCJT( ref );
            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendIntField( resp, "temp", temp );
                AppendRaw( resp, ", " );
                AppendIntField( resp, "cjt", cjt );
                AppendRaw( resp, " }" );
            }
        }
    }
#endif

    return resp;
}

// ============================================================================
// Thermistor
// ============================================================================

static APIPBPtr HandleThermistorGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "thermistorI2C", name );
    if ( raw != DRIVER_INSTANCE_NONE ) {
#if !FEATURE_THERMISTOR_HEATSINK
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        ThermistorI2CRef ref = TMI2COpen( raw, NULL, NULL );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            int32_t temp = TMI2CGetTemperature( ref );
            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendIntField( resp, "temp", temp );
                AppendRaw( resp, " }" );
            }
        }
#endif
    } else {
        raw = DriverFindInstance( "thermistor", name );
        if ( raw == DRIVER_INSTANCE_NONE ) {
            resp = APIResponseStatus( pb, APIStatusNotFound );
        } else {
#if !FEATURE_THERMISTORS
            resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
            ThermistorRef ref = TMOpen( (ThermistorID)raw );
            if ( !ref ) {
                resp = APIResponseStatus( pb, APIStatusUnavailable );
            } else {
                int32_t temp = TMGetTemperature( ref );
                resp = AcquirePB();
                if ( resp ) {
                    resp->status     = APIStatusOK;
                    resp->syntax     = pb->syntax;
                    resp->terminator = pb->terminator;
                    AppendRaw( resp, "{ " );
                    AppendIntField( resp, "temp", temp );
                    AppendRaw( resp, " }" );
                }
            }
#endif
        }
    }

    return resp;
}

// ============================================================================
// Tachometer
// ============================================================================

static APIPBPtr HandleTachometerGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "acFan", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
#if !FEATURE_TACHOMETER_OVEN_FAN
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        ACFanTachRef ref = ACFanTachOpen( raw, NULL, NULL );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            int32_t rpm = ACFanTachRead( ref );
            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendIntField( resp, "rpm", rpm );
                AppendRaw( resp, " }" );
            }
        }
#endif
    }

    return resp;
}

// ============================================================================
// Fan
// ============================================================================

static APIPBPtr HandleFanGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "acFan", name );
    if ( raw != DRIVER_INSTANCE_NONE ) {
#if !FEATURE_OVEN_FAN
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        ACFanRef ref = ACFanOpen( (ACFanID)raw, TriacOvenFan );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            uint32_t st = ACFanGetStatus( ref );
            int on = ( st & BIT( FlagTriacStatusActive ) ) ? 1 : 0;
            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendIntField( resp, "power", on ? 100 : 0 );
                AppendRaw( resp, ", " );
                AppendBoolField( resp, "on", on );
                AppendRaw( resp, " }" );
            }
        }
#endif
    } else {
        raw = DriverFindInstance( "dcFan", name );
        if ( raw == DRIVER_INSTANCE_NONE ) {
            resp = APIResponseStatus( pb, APIStatusNotFound );
        } else {
#if !FEATURE_BOARD_FAN
            resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
            DCFanRef ref = DCFanOpen( (DCFanID)raw, NULL );
            if ( !ref ) {
                resp = APIResponseStatus( pb, APIStatusUnavailable );
            } else {
                uint16_t rpm = DCFanGetSpeed( ref );
                uint32_t st  = DCFanGetStatus( ref );
                resp = AcquirePB();
                if ( resp ) {
                    resp->status     = APIStatusOK;
                    resp->syntax     = pb->syntax;
                    resp->terminator = pb->terminator;
                    AppendRaw( resp, "{ " );
                    AppendIntField( resp, "rpm", (int32_t)rpm );
                    AppendRaw( resp, ", " );
                    AppendBoolField( resp, "on", ( st & BIT( 0 ) ) ? 1 : 0 );
                    AppendRaw( resp, " }" );
                }
            }
#endif
        }
    }

    return resp;
}

static APIPBPtr HandleFanSet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "acFan", name );
    if ( raw != DRIVER_INSTANCE_NONE ) {
#if !FEATURE_OVEN_FAN
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        // TODO: ACFanOpen( (ACFanID)raw, TriacOvenFan ), parse power (0–100) from body, call ACFanSetSpeed
        resp = APIResponseStatus( pb, APIStatusOK );
#endif
    } else {
        raw = DriverFindInstance( "dcFan", name );
        if ( raw == DRIVER_INSTANCE_NONE ) {
            resp = APIResponseStatus( pb, APIStatusNotFound );
        } else {
#if !FEATURE_BOARD_FAN
            resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
            // TODO: DCFanOpen( (DCFanID)raw, NULL ), parse power (0–100) from body, call DCFanSetSpeed
            resp = APIResponseStatus( pb, APIStatusOK );
#endif
        }
    }

    return resp;
}

// ============================================================================
// Light
// ============================================================================

static APIPBPtr HandleLightGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "acLight", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
#if !FEATURE_OVEN_LIGHT
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        ACLightRef ref = ACLightOpen( (ACLightID)raw );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            uint32_t st = ACLightGetStatus( ref );
            int on = ( st & BIT( FlagTriacStatusActive ) ) ? 1 : 0;
            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendIntField( resp, "brightness", on ? 100 : 0 );
                AppendRaw( resp, ", " );
                AppendBoolField( resp, "on", on );
                AppendRaw( resp, " }" );
            }
        }
#endif
    }

    return resp;
}

static APIPBPtr HandleLightSet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "acLight", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
#if !FEATURE_OVEN_LIGHT
        resp = APIResponseStatus( pb, APIStatusNotImplemented );
#else
        // TODO: ACLightOpen( (ACLightID)raw ), parse brightness (0–100) from body, call ACLightSetPower
        resp = APIResponseStatus( pb, APIStatusOK );
#endif
    }

    return resp;
}

// ============================================================================
// Oven Controller
// ============================================================================

static APIPBPtr HandleOvenGet( APIPBPtr pb, const char* name ) {
    APIPBPtr resp = NULL;

    uint16_t raw = DriverFindInstance( "oc", name );
    if ( raw == DRIVER_INSTANCE_NONE ) {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    } else {
        OvenControllerRef ref = OCOpen( (OvenControllerID)raw );
        if ( !ref ) {
            resp = APIResponseStatus( pb, APIStatusUnavailable );
        } else {
            uint32_t st = OCGetStatus( ref );
            int atTemp  = ( st & BIT( FlagOvenControllerStatusAtTemp ) )  ? 1 : 0;
            int active  = ( st & BIT( FlagOvenControllerStatusActive ) )  ? 1 : 0;
            int fault   = ( st & BIT( FlagOvenControllerStatusFault ) )   ? 1 : 0;

            resp = AcquirePB();
            if ( resp ) {
                resp->status     = APIStatusOK;
                resp->syntax     = pb->syntax;
                resp->terminator = pb->terminator;
                AppendRaw( resp, "{ " );
                AppendBoolField( resp, "atTemp", atTemp );
                AppendRaw( resp, ", " );
                AppendBoolField( resp, "active", active );
                AppendRaw( resp, ", " );
                AppendBoolField( resp, "fault", fault );
                AppendRaw( resp, " }" );
            }
        }
    }

    return resp;
}

// ============================================================================
// Public dispatch handlers
// ============================================================================

APIPBPtr HandlerDeviceList( APIPBPtr pb ) {
    APIPBPtr resp = AcquirePB();
    if ( resp ) {
        resp->status     = APIStatusOK;
        resp->syntax     = pb->syntax;
        resp->terminator = pb->terminator;
        AppendRaw( resp, "{ \"instances\": [] }" );
    }
    return resp;
}

/// @brief Look up a DeviceTypeEntry by type string.
///        Returns NULL if no match is found.
static DeviceTypeEntryPtr FindDeviceEntry( const char* type ) {
    for ( size_t i = 0; i < kDeviceTypeCount; i++ ) {
        if ( strcmp( type, deviceTypes[ i ].type ) == 0 ) {
            return &deviceTypes[ i ];
        }
    }
    return NULL;
}

APIPBPtr HandlerDeviceGet( APIPBPtr pb ) {
    static char type[ 32 ];
    static char name[ 32 ];

    if ( sscanf( (const char*)pb->reqString, pb->route->pattern, type, name ) < 2 ) {
        return APIResponseStatus( pb, APIStatusBadRequest );
    }

    DeviceTypeEntryPtr entry = FindDeviceEntry( type );

    APIPBPtr resp;
    if ( entry && entry->get ) {
        resp = entry->get( pb, name );
    } else {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    }
    return resp;
}

APIPBPtr HandlerDeviceSet( APIPBPtr pb ) {
    static char type[ 32 ];
    static char name[ 32 ];

    if ( sscanf( (const char*)pb->reqString, pb->route->pattern, type, name ) < 2 ) {
        return APIResponseStatus( pb, APIStatusBadRequest );
    }

    const DeviceTypeEntry* entry = FindDeviceEntry( type );

    APIPBPtr resp;
    if ( entry && entry->set ) {
        resp = entry->set( pb, name );
    } else {
        resp = APIResponseStatus( pb, APIStatusNotFound );
    }
    return resp;
}
