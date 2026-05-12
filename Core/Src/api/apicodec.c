#include <ctype.h>
#include <string.h>

#include "apicodec.h"
#include "apicore.h"

#define APICODEC_C
#include "apiroutes.h"

// Parser state persisted across incoming buffers
typedef struct {
    APIPBPtr   wipPB;
    uint32_t   matchPos;
    uint32_t   candidateCount;
    uint8_t    candidates[ API_ROUTE_TABLE_SIZE ];
    bool       inPayload;     // Match resolved; consuming remainder as payload
    bool       inRaw;         // Past the % in the pattern; copying into rawRequest
    uint32_t   rawIdx;        // Write index into wipPB->rawRequest
    PayloadPtr lastPayload;
    uint32_t   payloadIdx;
    bool       lastWasSpace;
    bool       pendingCR;     // CR seen at end of last buffer; waiting to check for LF
} ParseState;

// Tracker for the serialisation state across multiple chunks
typedef struct {
    APIBufferPtr head;
    APIBufferPtr current;
    bool         error;
} SerialState;

static ParseState ps;

// Internal Prototypes
static void        ResetParser( void );
static bool        AppendPayloadByte( char c );
static void        TerminateRequest( TerminatorType term );
static inline bool PeekIsLF( const uint8_t* data, uint32_t len, uint32_t i );
static const char* GetStatusMessage( APIStatus status );
static void        AppendBlock( SerialState* state, const char* src, size_t len );
static void        AppendInt( SerialState* state, uint32_t val );
static void        AppendTerminator( SerialState* state, TerminatorType term );
static bool        SerialiseAPI( APIPBPtr pb );
static bool        SerialiseCLI( APIPBPtr pb );

// Init

void APIStreamInit( void ) {
    ResetParser();
}

// Parser

static void ResetParser( void ) {
    if ( ps.wipPB ) {
        ReleasePB( ps.wipPB );
    }

    memset( &ps, 0, sizeof( ps ) );

    for ( uint32_t i = 0; i < API_ROUTE_TABLE_SIZE; i++ ) {
        ps.candidates[ ps.candidateCount++ ] = (uint8_t)i;
    }
}

static inline bool PeekIsLF( const uint8_t* data, uint32_t len, uint32_t i ) {
    return ( i + 1 < len ) && ( data[ i + 1 ] == '\n' );
}

static inline bool AppendPayloadByte( char c ) {
    if ( !ps.lastPayload || ps.payloadIdx >= API_PAYLOAD_SIZE - 1 ) {
        PayloadPtr next = AcquirePayload();
        if ( !next ) return false;
        if ( !ps.wipPB->payload ) ps.wipPB->payload = next;
        if ( ps.lastPayload ) ps.lastPayload->next = next;
        ps.lastPayload = next;
        ps.payloadIdx  = 0;
    }
    ps.lastPayload->data[ ps.payloadIdx++ ] = c;
    return true;
}

static void TerminateRequest( TerminatorType term ) {
    if ( !ps.wipPB ) {
        ResetParser();
        return;
    }

    ps.wipPB->terminator = term;

    if ( ps.inPayload && ps.lastPayload ) {
        ps.lastPayload->data[ ps.payloadIdx ] = '\0';
    }

    if ( ps.inRaw ) {
        ps.wipPB->rawRequest[ ps.rawIdx ] = '\0';
    }

    if ( ps.wipPB->route || ps.wipPB->status == API_STATUS_NOT_FOUND ) {
        EnqueuePB( GetInputQueue(), ps.wipPB );
        ps.wipPB = NULL;
    }

    ResetParser();
}

void ProcessStream( const uint8_t* data, uint32_t len ) {
    for ( uint32_t i = 0; i < len; i++ ) {
        char c = (char)data[ i ];

        // Resolve a CR that arrived at the end of the previous buffer
        if ( ps.pendingCR ) {
            ps.pendingCR = false;
            if ( c == '\n' ) {
                TerminateRequest( TypeCRLF );
                continue;
            } else {
                TerminateRequest( TypeCR );
                // Fall through to process current byte normally
            }
        }

        // CR: peek ahead within this buffer, or defer if at end
        if ( c == '\r' ) {
            if ( PeekIsLF( data, len, i ) ) {
                i++;                            // consume the LF
                TerminateRequest( TypeCRLF );
            } else if ( i + 1 == len ) {
                ps.pendingCR = true;            // LF may arrive in next buffer
            } else {
                TerminateRequest( TypeCR );     // next char exists and is not LF
            }
            continue;
        }

        // Bare LF or null
        if ( c == '\n' || c == '\0' ) {
            TerminateRequest( TypeLF );
            continue;
        }

        // Normalise to lowercase
        char n = (char)tolower( (unsigned char)c );

        // Collapse space runs
        if ( n == ' ' ) {
            if ( ps.lastWasSpace ) continue;
            ps.lastWasSpace = true;
        } else {
            ps.lastWasSpace = false;
        }

        // Acquire PB if we don't have one
        if ( !ps.wipPB ) {
            ps.wipPB = AcquirePB();
            if ( !ps.wipPB ) {
                ResetParser();
                return;
            }
        }

        // If match resolved and we are past the %, copy into rawRequest
        if ( ps.inRaw ) {
            if ( n == '{' ) {
                // JSON body begins: switch to payload
                ps.wipPB->rawRequest[ ps.rawIdx ] = '\0';
                ps.inRaw     = false;
                ps.inPayload = true;
                AppendPayloadByte( n );
            } else if ( ps.rawIdx < API_REQUEST_MAX_LEN - 1 ) {
                ps.wipPB->rawRequest[ ps.rawIdx++ ] = n;
            }
            continue;
        }

        // If match fully resolved with no %, feed remainder into payload
        if ( ps.inPayload ) {
            AppendPayloadByte( n );
            continue;
        }

        // Prune candidates
        uint32_t surviving = 0;
        bool     resolved  = false;

        for ( uint32_t j = 0; j < ps.candidateCount; j++ ) {
            const APIRoute* route   = &apiRouteTable[ ps.candidates[ j ] ];
            const char*     pattern = route->pattern;

            if ( !pattern ) continue;

            size_t patLen = strlen( pattern );
            if ( ps.matchPos >= patLen ) continue;

            char p = (char)tolower( (unsigned char)pattern[ ps.matchPos ] );

            if ( p == '%' ) {
                // Wildcard: resolve immediately, start copying into rawRequest
                ps.wipPB->route           = route;
                ps.wipPB->origin          = API_MODE_API;
                ps.inRaw                  = true;
                ps.rawIdx                 = 0;
                ps.candidateCount         = 0;
                if ( ps.rawIdx < API_REQUEST_MAX_LEN - 1 ) {
                    ps.wipPB->rawRequest[ ps.rawIdx++ ] = n;
                }
                resolved = true;
                break;
            }

            if ( p == n ) {
                ps.candidates[ surviving++ ] = ps.candidates[ j ];
            }
        }

        if ( resolved ) continue;

        ps.candidateCount = surviving;
        ps.matchPos++;

        // Check if any surviving candidate is now fully matched
        for ( uint32_t j = 0; j < ps.candidateCount; j++ ) {
            const APIRoute* route   = &apiRouteTable[ ps.candidates[ j ] ];
            const char*     pattern = route->pattern;

            if ( !pattern ) continue;

            if ( ps.matchPos == strlen( pattern ) ) {
                ps.wipPB->route   = route;
                ps.wipPB->origin  = API_MODE_API;
                ps.inPayload      = true;
                ps.candidateCount = 0;
                resolved          = true;
                break;
            }
        }

        // No candidates and no resolution: unknown route
        if ( ps.candidateCount == 0 && !ps.inPayload && !ps.inRaw ) {
            if ( ps.wipPB ) {
                ps.wipPB->status = API_STATUS_NOT_FOUND;
                ps.wipPB->route  = NULL;
                EnqueuePB( GetInputQueue(), ps.wipPB );
                ps.wipPB = NULL;
            }
            ResetParser();
        }
    }
}

// Pull the next complete request from the queue for processing, or NULL if none available.
// Caller must return the PB to the pool when done with ReleasePB()
APIPBPtr GetNextRequest( void ) {
    return DequeuePB( GetInputQueue() );
}

// Outbound

static inline const char* GetStatusMessage( APIStatus status ) {
    switch ( status ) {
        case API_STATUS_OK:             return "OK";
        case API_STATUS_CREATED:        return "Created";
        case API_STATUS_NO_CONTENT:     return "No Content";
        case API_STATUS_BAD_REQUEST:    return "Bad Request";
        case API_STATUS_NOT_FOUND:      return "Not Found";
        case API_STATUS_CONFLICT:       return "Conflict";
        case API_STATUS_INTERNAL_ERROR: return "Internal Error";
        default:                        return "Unknown";
    }
}

static inline void AppendBlock( SerialState* state, const char* src, size_t len ) {
    while ( len > 0 && !state->error ) {
        size_t space = API_BUFFER_SIZE - state->current->length;

        if ( space == 0 ) {
            APIBufferPtr next = AcquireBuffer();
            if ( !next ) {
                state->error = true;
                return;
            }
            state->current->next = next;
            state->current       = next;
            space                = API_BUFFER_SIZE;
        }

        size_t toCopy = ( len < space ) ? len : space;
        memcpy( &state->current->data[ state->current->length ], src, toCopy );
        state->current->length += toCopy;
        src += toCopy;
        len -= toCopy;
    }
}

static void AppendInt( SerialState* state, uint32_t val ) {
    char buf[ 12 ];
    int  i = 0;

    if ( val == 0 ) {
        AppendBlock( state, "0", 1 );
        return;
    }

    while ( val > 0 && i < 10 ) {
        buf[ i++ ] = (char)( ( val % 10 ) + '0' );
        val /= 10;
    }

    while ( i > 0 ) {
        char ch = buf[ --i ];
        AppendBlock( state, &ch, 1 );
    }
}

static inline void AppendTerminator( SerialState* state, TerminatorType term ) {
    switch ( term ) {
        case TypeCRLF: AppendBlock( state, "\r\n", 2 ); break;
        case TypeCR:   AppendBlock( state, "\r",   1 ); break;
        case TypeLF:
        default:       AppendBlock( state, "\n",   1 ); break;
    }
}

static bool SerialiseAPI( APIPBPtr pb ) {
    SerialState state = { 0 };
    state.head = AcquireBuffer();
    if ( !state.head ) return false;
    state.current = state.head;

    AppendBlock( &state, "{ \"status\": ", 12 );
    AppendInt( &state, (uint32_t)pb->status );
    AppendBlock( &state, ", \"message\": \"", 14 );
    const char* msg = GetStatusMessage( pb->status );
    AppendBlock( &state, msg, strlen( msg ) );
    AppendBlock( &state, "\", \"data\": ", 11 );

    if ( pb->payload ) {
        PayloadPtr p = pb->payload;
        while ( p && !state.error ) {
            AppendBlock( &state, p->data, strlen( p->data ) );
            p = p->next;
        }
    } else {
        AppendBlock( &state, "null", 4 );
    }

    AppendBlock( &state, " }", 2 );
    AppendTerminator( &state, pb->terminator );

    if ( state.error ) {
        APIBufferPtr b = state.head;
        while ( b ) {
            APIBufferPtr next = b->next;
            ReleaseBuffer( b );
            b = next;
        }
        return false;
    }

    EnqueueBuffer( GetOutputQueue(), state.head );
    return true;
}

static bool SerialiseCLI( APIPBPtr pb ) {
    SerialState state = { 0 };
    state.head = AcquireBuffer();
    if ( !state.head ) return false;
    state.current = state.head;

    const char* prompt = ( pb->status < 400 ) ? "\r\n[OK] " : "\r\n[ERR] ";
    AppendBlock( &state, prompt, strlen( prompt ) );

    if ( pb->payload ) {
        PayloadPtr p = pb->payload;
        while ( p && !state.error ) {
            AppendBlock( &state, p->data, strlen( p->data ) );
            p = p->next;
        }
    }

    AppendBlock( &state, "\r\n> ", 4 );

    if ( state.error ) {
        APIBufferPtr b = state.head;
        while ( b ) {
            APIBufferPtr next = b->next;
            ReleaseBuffer( b );
            b = next;
        }
        return false;
    }

    EnqueueBuffer( GetOutputQueue(), state.head );
    return true;
}

void APIQueueForSend( APIPBPtr pb ) {
    if ( !pb ) return;

    if ( pb->origin == API_MODE_API ) {
        SerialiseAPI( pb );
    } else {
        SerialiseCLI( pb );
    }

    ReleasePBMembers( pb );
}
