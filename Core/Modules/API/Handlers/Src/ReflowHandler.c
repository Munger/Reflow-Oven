/// @file ReflowHandler.c
///
/// @brief Handler implementations for the /reflow endpoint group.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "APIHandlers.h"
#include "APICore.h"
#include "Reflow.h"
#include "ReflowProfile.h"

// ============================================================================
// Local helpers
// ============================================================================

static void PayloadToString( APIPBPtr pb, char* buf, size_t size ) {
    size_t pos = 0;
    for ( PayloadPtr p = pb->payload; p && pos < size - 1; p = p->next ) {
        for ( const char* s = p->data; *s >= 0x20 && pos < size - 1; ++s ) {
            buf[ pos++ ] = *s;
        }
    }
    buf[ pos ] = '\0';
}

// ============================================================================
// Active profile tracking
// ============================================================================

static char activeProfile[ 64 ] = { 0 };

// ============================================================================
// Public handlers
// ============================================================================

APIPBPtr HandlerReflowStatus( APIPBPtr pb ) {
    uint32_t flags = 0;
    if ( ReflowFlagsHandle ) {
        flags = osEventFlagsGet( ReflowFlagsHandle );
    }

    APIPBPtr resp = AcquirePB();
    if ( resp ) {
        resp->status     = APIStatusOK;
        resp->syntax     = pb->syntax;
        resp->terminator = pb->terminator;
        resp->payload    = NULL;

        int running    = ( flags & BIT( FlagReflowRunning ) )    ? 1 : 0;
        int holdActive = ( flags & BIT( FlagReflowHoldActive ) ) ? 1 : 0;
        int done       = ( flags & BIT( FlagReflowDone ) )       ? 1 : 0;

        PayloadPtr node = AcquirePayload();
        if ( node ) {
            node->next = NULL;
            size_t pos   = 0;
            char*  d     = node->data;
            size_t cap   = kApiPayloadSize - 1;

            d[ pos++ ] = '{'; d[ pos++ ] = ' ';
            d[ pos++ ] = '"';
            memcpy( d + pos, "running", 7 ); pos += 7;
            d[ pos++ ] = '"'; d[ pos++ ] = ':'; d[ pos++ ] = ' ';
            if ( running ) { memcpy( d + pos, "true", 4 ); pos += 4; }
            else           { memcpy( d + pos, "false", 5 ); pos += 5; }
            d[ pos++ ] = ','; d[ pos++ ] = ' ';

            d[ pos++ ] = '"';
            memcpy( d + pos, "holdActive", 10 ); pos += 10;
            d[ pos++ ] = '"'; d[ pos++ ] = ':'; d[ pos++ ] = ' ';
            if ( holdActive ) { memcpy( d + pos, "true", 4 ); pos += 4; }
            else              { memcpy( d + pos, "false", 5 ); pos += 5; }
            d[ pos++ ] = ','; d[ pos++ ] = ' ';

            d[ pos++ ] = '"';
            memcpy( d + pos, "done", 4 ); pos += 4;
            d[ pos++ ] = '"'; d[ pos++ ] = ':'; d[ pos++ ] = ' ';
            if ( done ) { memcpy( d + pos, "true", 4 ); pos += 4; }
            else        { memcpy( d + pos, "false", 5 ); pos += 5; }
            d[ pos++ ] = ','; d[ pos++ ] = ' ';

            d[ pos++ ] = '"';
            memcpy( d + pos, "profile", 7 ); pos += 7;
            d[ pos++ ] = '"'; d[ pos++ ] = ':'; d[ pos++ ] = ' '; d[ pos++ ] = '"';
            size_t plen = strlen( activeProfile );
            size_t copy = ( plen < cap - pos ) ? plen : ( cap - pos );
            if ( copy > 0 ) memcpy( d + pos, activeProfile, copy );
            pos += copy;
            d[ pos++ ] = '"';
            d[ pos++ ] = ' ';
            d[ pos++ ] = '}';
            d[ pos ] = '\0';

            resp->payload = node;
        } else {
            ReleasePB( resp );
            resp = NULL;
        }
    }
    return resp;
}

APIPBPtr HandlerReflowRun( APIPBPtr pb ) {
    static char name[ 64 ];
    PayloadToString( pb, name, sizeof( name ) );

    char* start = name;
    while ( *start == ' ' || *start == '\t' ) ++start;
    size_t len = strlen( start );
    while ( len > 0 && ( start[ len - 1 ] == ' ' || start[ len - 1 ] == '\t' ) ) {
        start[ --len ] = '\0';
    }

    const char* profileName = ( *start ) ? start : NULL;

    if ( !ReflowStart( profileName ) ) {
        return APIResponseStatus( pb, APIStatusConflict );
    }

    if ( profileName ) {
        size_t slen = strlen( profileName );
        if ( slen >= sizeof( activeProfile ) ) slen = sizeof( activeProfile ) - 1;
        memcpy( activeProfile, profileName, slen );
        activeProfile[ slen ] = '\0';
    }

    return APIResponseStatus( pb, APIStatusAccepted );
}

APIPBPtr HandlerReflowStop( APIPBPtr pb ) {
    if ( ReflowFlagsHandle ) {
        osEventFlagsSet( ReflowFlagsHandle, BIT( FlagReflowStop ) );
    }
    return APIResponseStatus( pb, APIStatusOK );
}
