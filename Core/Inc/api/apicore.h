#ifndef APICORE_H
#define APICORE_H

#include <stdint.h>

#include "apitypes.h"

/* Specific Opaque Handles - The compiler treats these as unique types */
typedef struct APIPBQueue*     APIPBQueueRef;
typedef struct APIBufferQueue* APIBufferQueueRef;

// Initialises the pools and queues. Call once at startup.
void                           APICoreInit( void );

// Requests received from the transport layer are enqueued here for processing
// by the main loop
APIPBQueueRef                  GetInputQueue( void );
// Completed requests are enqueued here for the transport layer to send back to
// the host
APIBufferQueueRef              GetOutputQueue( void );

// Grab an APIPB from the pool
APIPBPtr                       AcquirePB( void );

// Return an APIPB to the pool
void                           ReleasePB( APIPBPtr pb );

// Enqueue a completed APIPB to the specified queue
void                           EnqueuePB( APIPBQueueRef q, APIPBPtr pb );

// Dequeue a completed APIPB from the specified queue, or NULL if empty
APIPBPtr                       DequeuePB( APIPBQueueRef q );

// Enqueue a completed APIBuffer to the specified queue
void                           EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr pb );

// Dequeue a completed APIPB from the specified queue, or NULL if empty
APIBufferPtr                   DequeueBuffer( APIBufferQueueRef q );

// Grab a payload from the pool
PayloadPtr                     AcquirePayload( void );

// Return a payload to the pool
void                           ReleasePayload( PayloadPtr payload );

// Recycle the tokens and payloads attached to a PB (stack or pool)
void                           ReleasePBMembers( APIPBPtr pb );

// Grab a buffer from the pool
APIBufferPtr                   AcquireBuffer( void );

// Return a buffer to the pool
void                           ReleaseBuffer( APIBufferPtr buffer );

// Stats for debugging and monitoring
typedef struct APICoreStats {
    // Current counts
    uint32_t pbFree;
    uint32_t payloadFree;
    uint32_t bufferFree;
    uint32_t inputQueued;
    uint32_t outputQueued;

    // High-water marks (peak usage)
    uint32_t pbPeak;
    uint32_t payloadPeak;
    uint32_t bufferPeak;

    // Static sizing
    uint32_t pbCount;
    uint32_t payloadCount;
    uint32_t bufferCount;

    size_t   pbSize;
    size_t   payloadSize;
    size_t   bufferSize;

    // Total memory in use for each entity
    size_t   pbMemUsed;
    size_t   payloadMemUsed;
    size_t   bufferMemUsed;
} APICoreStats;

typedef const APICoreStats* APICoreStatsRef;

APICoreStatsRef             APICoreGetStats( void );

#endif // APICORE_H
