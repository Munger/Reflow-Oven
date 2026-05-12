#ifndef APITYPES_H
#define APITYPES_H

#include "types.h"

enum {
    API_PAYLOAD_SIZE = 512,
    API_PAYLOAD_COUNT = 64,
    APIPB_COUNT = 10,
    API_BUFFER_SIZE = 512,
    API_BUFFER_COUNT = 8,
    API_REQUEST_MAX_LEN = 256
};

typedef enum {
    API_STATUS_OK = 200,
    API_STATUS_CREATED = 201,
    API_STATUS_NO_CONTENT = 204,
    API_STATUS_BAD_REQUEST = 400,
    API_STATUS_NOT_FOUND = 404,
    API_STATUS_CONFLICT = 409,
    API_STATUS_INTERNAL_ERROR = 500
} APIStatus;

typedef enum {
    API_MODE_UNDETERMINED,
    API_MODE_API,
    API_MODE_CLI
} APIMode;

// HTTP methods

typedef enum {
    API_METHOD_UNKNOWN = 0,
    API_METHOD_GET,
    API_METHOD_POST,
    API_METHOD_PUT,
    API_METHOD_DELETE
} APIMethod;

typedef enum {
    TypeNull,
    TypeLF,
    TypeCR,
    TypeCRLF
} TerminatorType;

typedef struct Payload {
    struct Payload* next;
    char            data[ API_PAYLOAD_SIZE ];
} Payload, *PayloadPtr;

typedef struct APIPB {
    struct APIPB*          next;
    APIStatus              status;
    uint8_t                rawRequest[ API_REQUEST_MAX_LEN ];
    const struct APIRoute* route;
    PayloadPtr             payload;
    APIMode                origin;
    TerminatorType         terminator;
} APIPB, *APIPBPtr;

typedef struct APIBuffer {
    struct APIBuffer* next; // Must be FIRST
    char              data[ API_BUFFER_SIZE ];
    size_t            length;
} APIBuffer, *APIBufferPtr;

#endif // APITYPES_H