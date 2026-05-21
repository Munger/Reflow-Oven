/// @file APICodec.c
///
/// @brief USB stream parser and response serialiser for the REST-over-CDC API.
///
/// ProcessStream() implements an incremental, stateful route-matching parser that
/// consumes raw CDC receive buffers and assembles complete APIPB requests. Route
/// patterns are matched character-by-character against the apiRouteTable using a
/// candidate-pruning algorithm; a %-wildcard in a pattern switches to raw-copy
/// mode. Completed requests are pushed onto the input queue and the API task is
/// notified.
///
/// APIQueueForSend() serialises a completed APIPB response into one or more chained
/// APIBuffer objects and enqueues them for transmission. API-mode responses use JSON
/// ({status, message, data}); CLI-mode responses use a human-readable prompt format.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "APICodec.h"
#include "APICore.h"

static inline char safe_tolower( char c ) {
    return ( c >= 'A' && c <= 'Z' ) ? ( c + 32 ) : c;
}

#define APICODEC_C
#include "APIRoutes.h"

/// @brief Incremental parser state persisted across successive CDC receive buffers.
///
/// All fields are zeroed and candidates re-populated by ResetParser(). The struct
/// is kept as a single static instance because the stream is single-producer.
typedef struct {
    APIPBPtr        wipPB;                              ///< APIPB being assembled; NULL between requests.
    uint32_t        matchPos;                           ///< Number of pattern characters consumed so far.
    uint32_t        candidateCount;                     ///< Number of live route candidates.
    uint8_t         candidates[ API_ROUTE_TABLE_SIZE ]; ///< Indices into apiRouteTable still in contention.
    APIRoutePtr     bestMatch;                          ///< Most recently fully-matched non-% pattern; fallback if no wildcard wins.
    uint32_t        bestMatchPos;                       ///< matchPos at which bestMatch was saved (for reqString rollback).
    bool            inPayload;   ///< Match resolved with no %; remainder goes into payload chain.
    bool            inRaw;       ///< Past the % in a pattern; bypasses candidate pruning.
    uint32_t   reqIdx;      ///< Write index into wipPB->reqString.
    PayloadPtr lastPayload; ///< Tail of the payload chain being built.
    uint32_t   payloadIdx;  ///< Write index into the current payload node.
    bool       lastWasSpace;///< Used to collapse consecutive spaces into one.
    bool       pendingCR;   ///< CR arrived at end of last buffer; check next byte for LF.
} ParseState;

/// @brief Serialisation cursor tracking the APIBuffer chain being built for one response.
typedef struct {
    APIBufferPtr head;    ///< First buffer in the chain (enqueued when serialisation completes).
    APIBufferPtr current; ///< Buffer currently being written into.
    bool         error;   ///< Set true if any AcquireBuffer() call fails; triggers chain release.
} SerialState;

/// @brief Persistent parser state singleton.
static ParseState ps;

/// @brief Current output queue routing context — overrides GetOutputQueue() when set.
static APIBufferQueueRef currentOutputQueue = NULL;

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

/// @brief Initialise (or reset) the stream parser. Call once before ProcessStream().
void APIStreamInit( void ) {
    ResetParser();
}

// Parser

/// @brief Discard any work-in-progress PB and reset the parser to its initial state.
///
/// Re-populates the candidate array with all route table indices so the next byte
/// starts a fresh match attempt.
static void ResetParser( void ) {
    if ( ps.wipPB ) {
        ReleasePB( ps.wipPB );
    }

    memset( &ps, 0, sizeof( ps ) );

    for ( uint32_t i = 0; i < API_ROUTE_TABLE_SIZE; i++ ) {
        ps.candidates[ ps.candidateCount++ ] = (uint8_t)i;
    }
}

/// @brief Peek at the byte immediately after position @p i to check whether it is LF.
///
/// @param[in] data  Raw receive buffer.
/// @param[in] len   Total buffer length.
/// @param[in] i     Index of the current byte (the CR).
/// @return true if data[i+1] exists and equals '\n'.
static inline bool PeekIsLF( const uint8_t* data, uint32_t len, uint32_t i ) {
    return ( i + 1 < len ) && ( data[ i + 1 ] == '\n' );
}

/// @brief Append a single character to the payload chain of the work-in-progress PB.
///
/// Acquires a new Payload node when the current one is full. Returns false if
/// AcquirePayload() fails; the caller should abort and reset.
///
/// @param[in] c  Character to append.
/// @return true on success, false if the payload pool is exhausted.
static inline bool AppendPayloadByte( char c ) {
    if ( !ps.lastPayload || ps.payloadIdx >= kApiPayloadSize - 1 ) {
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

/// @brief Finalise and enqueue the current work-in-progress PB, then reset the parser.
///
/// NUL-terminates the payload or reqString if either is open. Enqueues the PB only
/// if a route was matched or a 404 status was set; otherwise the PB is discarded.
///
/// @param[in] term  Line terminator type detected by the caller.
static void TerminateRequest( TerminatorType term ) {
    if ( !ps.wipPB ) {
        ResetParser();
        return;
    }

    ps.wipPB->terminator = term;

    // No %-wildcard or bestMatch resolution happened — apply bestMatch now
    if ( !ps.wipPB->route && ps.bestMatch ) {
        ps.wipPB->route  = ps.bestMatch;
        ps.wipPB->syntax = ps.bestMatch->syntax;
        ps.wipPB->reqString[ ps.bestMatchPos ] = '\0';
        ps.reqIdx        = ps.bestMatchPos;
        ps.inPayload     = true;
        ps.bestMatch     = NULL;
    }

    if ( ps.inPayload && ps.lastPayload ) {
        ps.lastPayload->data[ ps.payloadIdx ] = '\0';
    }

    if ( ps.inRaw ) {
        ps.wipPB->reqString[ ps.reqIdx ] = '\0';
    }

    if ( ps.wipPB->route || ps.wipPB->status == APIStatusNotFound ) {
        EnqueuePB( GetInputQueue(), ps.wipPB );
        ps.wipPB = NULL;
    }

    ResetParser();
}

/// @brief Consume a raw CDC receive buffer and advance the parser state machine.
///
/// Each byte is lower-cased and tested against the surviving route candidates.
/// Consecutive spaces are collapsed. CR/LF handling supports bare CR, bare LF,
/// and CRLF sequences that may span buffer boundaries. Called directly from the
/// CDC_Receive_FS() USB ISR callback.
void ProcessStream( const uint8_t* data, uint32_t len ) {
    for ( uint32_t i = 0; i < len; i++ ) {
        char c = (char)data[ i ];

        // Resolve a CR that arrived at the end of the previous buffer
        if ( ps.pendingCR ) {
            ps.pendingCR = false;
            if ( c == '\n' ) {
                TerminateRequest( TermCRLF );
                continue;
            } else {
                TerminateRequest( TermCR );
                // Fall through to process current byte normally
            }
        }

        // CR: peek ahead within this buffer, or defer if at end
        if ( c == '\r' ) {
            if ( PeekIsLF( data, len, i ) ) {
                i++;                            // consume the LF
                TerminateRequest( TermCRLF );
            } else if ( i + 1 == len ) {
                ps.pendingCR = true;            // LF may arrive in next buffer
            } else {
                TerminateRequest( TermCR );     // next char exists and is not LF
            }
            continue;
        }

        if ( c == '\n' ) {
            TerminateRequest( TermLF );
            continue;
        }

        if ( c == '\0' ) {
            TerminateRequest( TermZero );
            continue;
        }

        // Normalise to lowercase
        char n = safe_tolower( c );

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

        // Capture every byte into reqString until we enter payload-only mode.
        // Handlers later extract typed arguments via sscanf(reqString, route->pattern, ...).
        if ( !ps.inPayload && ps.reqIdx < kApiRequestMaxLen - 1 ) {
            ps.wipPB->reqString[ ps.reqIdx++ ] = n;
        }

        // Past the % wildcard — skip candidate pruning, handle body transition only
        if ( ps.inRaw ) {
            if ( n == '{' ) {
                // JSON body begins: remove '{' from reqString, feed into payload instead
                ps.wipPB->reqString[ --ps.reqIdx ] = '\0';
                ps.inRaw     = false;
                ps.inPayload = true;
                AppendPayloadByte( n );
            }
            continue;
        }

        // Match fully resolved with no % — feed remainder into payload
        if ( ps.inPayload ) {
            AppendPayloadByte( n );
            continue;
        }

        // Prune candidates
        uint32_t surviving = 0;
        bool     resolved  = false;

        for ( uint32_t j = 0; j < ps.candidateCount; j++ ) {
            APIRoutePtr route   = &apiRouteTable[ ps.candidates[ j ] ];
            const char*     pattern = route->pattern;

            if ( !pattern ) continue;

            size_t patLen = strlen( pattern );
            if ( ps.matchPos >= patLen ) continue;

            char p = safe_tolower( pattern[ ps.matchPos ] );

            if ( p == '%' ) {
                // Wildcard: resolve immediately; reqString already contains every byte
                // (including this one) thanks to the pre-resolution capture above.
                ps.wipPB->route   = route;
                ps.wipPB->syntax  = route->syntax;
                ps.inRaw          = true;
                ps.candidateCount = 0;
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

        // Save any fully-matched non-% pattern as bestMatch fallback.
        // Keep pruning remaining candidates — a longer pattern with a %
        // wildcard that extends this prefix may still win.
        for ( uint32_t j = 0; j < ps.candidateCount; j++ ) {
            const APIRoute* route   = &apiRouteTable[ ps.candidates[ j ] ];
            const char*     pattern = route->pattern;

            if ( !pattern ) continue;

            if ( ps.matchPos == strlen( pattern ) ) {
                ps.bestMatch    = route;
                ps.bestMatchPos = ps.matchPos;
                break;
            }
        }

        // All candidates exhausted with no wildcard resolution:
        // fall back to bestMatch if one was saved.
        if ( ps.candidateCount == 0 && !ps.inPayload && !ps.inRaw ) {
            if ( ps.bestMatch ) {
                ps.wipPB->route  = ps.bestMatch;
                ps.wipPB->syntax = ps.bestMatch->syntax;
                ps.wipPB->reqString[ ps.bestMatchPos ] = '\0';
                ps.reqIdx        = ps.bestMatchPos;
                ps.inPayload     = true;
                ps.bestMatch     = NULL;
            } else {
                ps.wipPB->status = APIStatusNotFound;
                ps.wipPB->route  = NULL;
                EnqueuePB( GetInputQueue(), ps.wipPB );
                ps.wipPB = NULL;
                ResetParser();
            }
        }
    }
}

/// @brief Dequeue the next complete request from the input queue, or NULL if none available.
///
/// The caller takes ownership and must call ReleasePB() when done.
///
/// @return Pointer to the oldest queued APIPB, or NULL if the queue is empty.
APIPBPtr GetNextRequest( void ) {
    return DequeuePB( GetInputQueue() );
}

// Outbound

/// @brief Map an APIStatus code to its human-readable string for JSON responses.
///
/// @param[in] status  Status code to look up.
/// @return Statically allocated string; never NULL.
static inline const char* GetStatusMessage( APIStatus status ) {
    switch ( status ) {
        case APIStatusOK:             return "OK";
        case APIStatusCreated:        return "Created";
        case APIStatusAccepted:       return "Accepted";
        case APIStatusNoContent:     return "No Content";
        case APIStatusBadRequest:    return "Bad Request";
        case APIStatusForbidden:     return "Forbidden";
        case APIStatusNotFound:      return "Not Found";
        case APIStatusConflict:       return "Conflict";
        case APIStatusUnprocessable: return "Unprocessable";
        case APIStatusNotImplemented: return "Not Implemented";
        case APIStatusInternalError: return "Internal Error";
        case APIStatusUnavailable:   return "Unavailable";
        default:                        return "Unknown";
    }
}

/// @brief Copy @p len bytes from @p src into the current SerialState buffer chain.
///
/// Acquires additional APIBuffer nodes as needed. Sets state->error on allocation
/// failure; subsequent calls become no-ops once the error flag is set.
///
/// @param[in,out] state  Serialisation cursor; error flag set on pool exhaustion.
/// @param[in]     src    Source bytes to append.
/// @param[in]     len    Number of bytes to append.
static inline void AppendBlock( SerialState* state, const char* src, size_t len ) {
    while ( len > 0 && !state->error ) {
        size_t space = kApiBufferSize - state->current->length;

        if ( space == 0 ) {
            APIBufferPtr next = AcquireBuffer();
            if ( !next ) {
                state->error = true;
                return;
            }
            state->current->next = next;
            state->current       = next;
            space                = kApiBufferSize;
        }

        size_t toCopy = ( len < space ) ? len : space;
        memcpy( &state->current->data[ state->current->length ], src, toCopy );
        state->current->length += toCopy;
        src += toCopy;
        len -= toCopy;
    }
}

/// @brief Append the decimal representation of @p val to the serialisation buffer chain.
///
/// @param[in,out] state  Serialisation cursor.
/// @param[in]     val    Unsigned integer to append.
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

/// @brief Append the line terminator matching @p term to the serialisation buffer chain.
///
/// @param[in,out] state  Serialisation cursor.
/// @param[in]     term   Terminator type echoed from the original request.
static inline void AppendTerminator( SerialState* state, TerminatorType term ) {
    switch ( term ) {
        case TermCRLF: AppendBlock( state, "\r\n", 2 ); break;
        case TermCR:   AppendBlock( state, "\r",   1 ); break;
        case TermZero: AppendBlock( state, "\0",   1 ); break;
        case TermLF:
        default:       AppendBlock( state, "\n",   1 ); break;
    }
}

/// @brief Serialise @p pb as a JSON object and enqueue the result for transmission.
///
/// Format: { "status": NNN, "message": "...", "data": payload_or_null } CR LF
/// The terminator type is echoed from the original request.
/// Releases all acquired buffers and returns false on allocation failure.
///
/// @param[in] pb  Completed APIPB with status and optional payload chain set.
/// @return true if serialisation succeeded and the buffer chain was enqueued.
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

    EnqueueBuffer( GetCurrentOutputQueue(), state.head );
    return true;
}

/// @brief Serialise @p pb as a human-readable CLI response and enqueue for transmission.
///
/// Format: CR LF [OK] payload CR LF >   or   CR LF [ERR] payload CR LF >
/// The prompt suffix keeps the terminal in an interactive state. Status codes
/// below 400 are treated as success.
///
/// @param[in] pb  Completed APIPB with status and optional payload chain set.
/// @return true if serialisation succeeded and the buffer chain was enqueued.
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

    EnqueueBuffer( GetCurrentOutputQueue(), state.head );
    return true;
}

/// @brief Serialise a completed APIPB and enqueue for USB transmission.
void APIQueueForSend( APIPBPtr pb ) {
    if ( !pb ) return;

    if ( pb->syntax == APISyntaxREST ) {
        SerialiseAPI( pb );
    } else {
        SerialiseCLI( pb );
    }

    ReleasePB( pb );
}

/// @brief Set the output queue routing context.
void SetCurrentOutputQueue( APIBufferQueueRef q ) {
    currentOutputQueue = q;
}

/// @brief Return the active output queue, falling back to the global default.
APIBufferQueueRef GetCurrentOutputQueue( void ) {
    return currentOutputQueue ? currentOutputQueue : GetOutputQueue();
}

/// @brief Reset to the default global output queue.
void ResetCurrentOutputQueue( void ) {
    currentOutputQueue = NULL;
}
