/// @file APITypes.h
///
/// @brief Core API type definitions shared across the codec, core, and handler layers.
///
/// Defines the fundamental types used by the USB REST / CLI API subsystem:
///   - Pool sizing constants (payload, protocol buffer, transmit buffer counts/sizes)
///   - `APIStatus` — HTTP-like response status codes
///   - `APIMode`   — request origin (API JSON vs. CLI text)
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
    API_PAYLOAD_SIZE    = 512, ///< Maximum bytes in a single Payload data chunk.
    API_PAYLOAD_COUNT   = 64,  ///< Total Payload objects in the static pool.
    APIPB_COUNT         = 10,  ///< Total APIPB protocol buffers in the static pool.
    API_BUFFER_SIZE     = 512, ///< Maximum bytes in a single APIBuffer transmit chunk.
    API_BUFFER_COUNT    = 8,   ///< Total APIBuffer objects in the static pool.
    API_REQUEST_MAX_LEN = 256  ///< Maximum bytes captured into APIPB.rawRequest.
};

/// @brief HTTP-like response status codes returned by handler functions.
typedef enum {
    API_STATUS_OK             = 200, ///< Request succeeded.
    API_STATUS_CREATED        = 201, ///< Resource created successfully.
    API_STATUS_NO_CONTENT     = 204, ///< Request succeeded; no body to return.
    API_STATUS_BAD_REQUEST    = 400, ///< Malformed request or invalid parameters.
    API_STATUS_NOT_FOUND      = 404, ///< No route matched the incoming request.
    API_STATUS_CONFLICT       = 409, ///< Request conflicts with current state.
    API_STATUS_INTERNAL_ERROR = 500  ///< Handler encountered an internal error.
} APIStatus;

/// @brief Request origin mode, used to select the serialisation format on output.
typedef enum {
    API_MODE_UNDETERMINED, ///< Mode has not yet been determined by the parser.
    API_MODE_API,          ///< Request arrived as a REST JSON command — serialise as JSON.
    API_MODE_CLI           ///< Request arrived as a CLI text command — serialise as a human-readable prompt.
} APIMode;

/// @brief HTTP verb parsed from the incoming request line.
typedef enum {
    API_METHOD_UNKNOWN = 0, ///< No verb recognised.
    API_METHOD_GET,         ///< HTTP GET.
    API_METHOD_POST,        ///< HTTP POST.
    API_METHOD_PUT,         ///< HTTP PUT.
    API_METHOD_DELETE       ///< HTTP DELETE.
} APIMethod;

/// @brief Line-ending style detected by the stream parser.
///
/// The parser mirrors the terminator back in the serialised response so that
/// the host-side line discipline receives a consistent output format regardless
/// of which convention it sent.
typedef enum {
    TypeNull, ///< No terminator detected (unused after full parse).
    TypeLF,   ///< Bare LF (0x0A).
    TypeCR,   ///< Bare CR (0x0D).
    TypeCRLF  ///< CR+LF pair (0x0D 0x0A).
} TerminatorType;

/// @brief Singly-linked data chunk for building multi-part response bodies.
///
/// Handlers acquire a chain of Payload objects from the pool via
/// `AcquirePayload()` and append to each `data` array in sequence.
/// The chain is owned by the APIPB until `ReleasePBMembers()` returns
/// all chunks to the pool.
typedef struct Payload {
    struct Payload* next;              ///< Next chunk in the chain, or NULL if this is the last.
    char            data[ API_PAYLOAD_SIZE ]; ///< Raw data bytes for this chunk.
} Payload, *PayloadPtr;

/// @brief Protocol buffer — the unit of work flowing through the API pipeline.
///
/// The stream parser populates one APIPB per complete incoming request.
/// The API task dispatches it to the matched handler, which writes response
/// data into the `payload` chain. `APIQueueForSend()` then serialises the
/// APIPB into one or more `APIBuffer` objects and enqueues them for USB TX.
typedef struct APIPB {
    struct APIPB*          next;                          ///< Queue linkage — must remain first.
    APIStatus              status;                        ///< Response status code written by the handler.
    uint8_t                rawRequest[ API_REQUEST_MAX_LEN ]; ///< Argument bytes captured after the `%` wildcard in the route pattern.
    const struct APIRoute* route;                         ///< Matched route entry, or NULL if not found.
    PayloadPtr             payload;                       ///< Head of the response body payload chain.
    APIMode                origin;                        ///< Request origin (API JSON or CLI text).
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
    char              data[ API_BUFFER_SIZE ]; ///< Serialised response bytes.
    size_t            length;            ///< Number of valid bytes in `data`.
} APIBuffer, *APIBufferPtr;

#endif // APITYPES_H
