/// @file APITypes.h
///
/// @brief Core API type definitions shared across the codec, core, and handler layers.
///
/// Defines the fundamental types used by the USB REST / CLI API subsystem:
///   - Pool sizing constants (payload, protocol buffer, transmit buffer counts/sizes)
///   - `APIStatus` — HTTP-like response status codes
///   - `APISyntax` — serialisation format declared per route
///   - `APIMethod` — HTTP verb (GET, POST, PUT, DELETE)
///   - `TerminatorType` — line-ending style detected by the stream parser
///   - `Payload`   — singly-linked data chunk for response bodies
///   - `APIPB`     — protocol buffer carrying a complete parsed request
///   - `APIBuffer` — fixed-size transmit buffer for outbound serialised responses
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APITYPES_H
#define APITYPES_H

#include "Types.h"

/// @brief API pool and buffer sizing constants.
enum {
    kApiPayloadSize    = 512, ///< Maximum bytes in a single Payload data chunk.
    kApiPayloadCount   = 64,  ///< Total Payload objects in the static pool.
    kApiPbCount         = 10,  ///< Total APIPB protocol buffers in the static pool.
    kApiBufferSize     = 512, ///< Maximum bytes in a single APIBuffer transmit chunk.
    kApiBufferCount    = 8,   ///< Total APIBuffer objects in the static pool.
    kApiRequestMaxLen = 256  ///< Maximum bytes captured into APIPB.reqString.
};

/// @brief HTTP-like response status codes returned by handler functions.
typedef enum {
    APIStatusOK             = 200, ///< Request succeeded.
    APIStatusCreated        = 201, ///< Resource created successfully.
    APIStatusAccepted       = 202, ///< Accepted — async operation started; poll for result.
    APIStatusNoContent     = 204, ///< Request succeeded; no body to return.
    APIStatusBadRequest    = 400, ///< Malformed request or invalid parameters.
    APIStatusForbidden     = 403, ///< Action not permitted in current oven state.
    APIStatusNotFound      = 404, ///< No route matched the incoming request.
    APIStatusConflict       = 409, ///< Request conflicts with current state.
    APIStatusUnprocessable = 422, ///< Request body is syntactically valid but semantically invalid.
    APIStatusInternalError = 500, ///< Handler encountered an internal error.
    APIStatusNotImplemented = 501, ///< Route is valid but the requested hardware is not fitted on this board.
    APIStatusUnavailable   = 503  ///< Service temporarily unavailable (e.g. filesystem busy).
} APIStatus;

/// @brief Serialisation syntax declared in each route entry.
typedef enum {
    APISyntaxREST, ///< Serialise as REST response (e.g. JSON or CBOR for a web client).
    APISyntaxCLI,  ///< Serialise as human-readable text for a terminal user.
} APISyntax;

/// @brief HTTP verb parsed from the incoming request line.
typedef enum {
    APIMethodUnknown = 0, ///< No verb recognised.
    APIMethodGet,         ///< HTTP GET.
    APIMethodPost,        ///< HTTP POST.
    APIMethodPut,         ///< HTTP PUT.
    APIMethodDelete       ///< HTTP DELETE.
} APIMethod;

/// @brief Line-ending style detected by the stream parser.
///
/// The parser mirrors the terminator back in the serialised response so that
/// the host-side line discipline receives a consistent output format regardless
/// of which convention it sent.
typedef enum {
    TermNull, ///< No terminator detected (sentinel; unused after a completed parse).
    TermLF,   ///< Bare LF (0x0A).
    TermCR,   ///< Bare CR (0x0D).
    TermCRLF, ///< CR+LF pair (0x0D 0x0A).
    TermZero  ///< Null byte (0x00); response is also null-terminated.
} TerminatorType;

/// @brief Singly-linked data chunk for building multi-part response bodies.
///
/// Handlers acquire a chain of Payload objects from the pool via
/// `AcquirePayload()` and append to each `data` array in sequence.
/// The chain is owned by the APIPB until `ReleasePBMembers()` returns
/// all chunks to the pool.
typedef struct Payload {
    struct Payload* next;              ///< Next chunk in the chain, or NULL if this is the last.
    char            data[ kApiPayloadSize ]; ///< Raw data bytes for this chunk.
} Payload, *PayloadPtr;

typedef const struct APIRoute *APIRoutePtr;

/// @brief Protocol buffer — the unit of work flowing through the API pipeline.
///
/// The stream parser populates one APIPB per complete incoming request.
/// The API task dispatches it to the matched handler, which writes response
/// data into the `payload` chain. `APIQueueForSend()` then serialises the
/// APIPB into one or more `APIBuffer` objects and enqueues them for USB TX.
typedef struct APIPB {
    struct APIPB*          next;                          ///< Queue linkage — must remain first.
    APIStatus              status;                        ///< Response status code written by the handler.
    uint8_t                reqString[ kApiRequestMaxLen ]; ///< Full sanitised (lowercased, space-collapsed) request line captured by the stream parser.
    APIRoutePtr            route;                         ///< Matched route entry, or NULL if not found.
    PayloadPtr             payload;                       ///< Head of the response body payload chain.
    APISyntax              syntax;                        ///< Serialisation format from the matched route.
    TerminatorType         terminator;                    ///< Line-ending style from the incoming request.
} APIPB, *APIPBPtr;

/// @brief Fixed-size outbound transmit buffer.
///
/// Serialised response bytes are written into chained APIBuffer objects by
/// `SerialiseAPI()` / `SerialiseCLI()`. The first field (`next`) must remain
/// at offset zero so the generic pool/queue machinery can treat it as a
/// singly-linked node.
typedef struct APIBuffer {
    struct APIBuffer* next;              ///< Queue linkage — must be FIRST.
    char              data[ kApiBufferSize ]; ///< Serialised response bytes.
    size_t            length;            ///< Number of valid bytes in `data`.
} APIBuffer, *APIBufferPtr;

#endif // APITYPES_H
