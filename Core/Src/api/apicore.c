#include <string.h>

#include "apicore.h"
#include "stm32g0xx.h"

// The actual structure definition
typedef struct APIPBQueue {
    APIPBPtr head;
    APIPBPtr tail;
    uint32_t count;
} APIPBQueue;

typedef struct APIBufferQueue {
    APIBufferPtr head;
    APIBufferPtr tail;
    uint32_t     count;
} APIBufferQueue;

#define APIPB_SIZE sizeof( APIPB )

// The Unified Engine State - Wrapped for easy reset and monitoring
static struct {
    // Static Storage (Physical RAM)
    APIPB          pbStorage[ APIPB_COUNT ];
    Payload        payloadStorage[ API_PAYLOAD_COUNT ];
    APIBuffer      bufferStorage[ API_BUFFER_COUNT ];

    // Pools (Just Head Pointers / LIFO)
    APIPBPtr       pbPool;
    PayloadPtr     payloadPool;
    APIBufferPtr   bufferPool;

    // The Active Queues (FIFO)
    APIPBQueue     inputQueue;
    APIBufferQueue outputQueue;

    // Telemetry
    APICoreStats   stats;
} engine;

// Internal Atomic Pool Logic

static void _poolPush( void** pool, void* item, uint32_t* count ) {

    if ( !item ) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    *(void**)item = *pool;
    *pool = item;

    if ( count ) {
        ( *count )++;
    }

    __set_PRIMASK( primask );
}

static void* _poolPop( void** pool, uint32_t* count, uint32_t* peak, uint32_t total ) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    void* item = *pool;

    if ( item ) {
        *pool = *(void**)item;

        if ( count ) {
            ( *count )--;
        }

        if ( count && peak ) {
            uint32_t inUse = total - *count;
            if ( inUse > *peak ) {
                *peak = inUse;
            }
        }
    }

    __set_PRIMASK( primask );
    return item;
}

// Initialises the pools and queues. Call once at startup.
void APICoreInit( void ) {
    // 1. Clear everything: Wipes storage, pointers, and stats in one go
    memset( &engine, 0, sizeof( engine ) );

    // 2. Initialise fixed stats
    engine.stats.pbCount = APIPB_COUNT;
    engine.stats.payloadCount = API_PAYLOAD_COUNT;
    engine.stats.bufferCount = API_BUFFER_COUNT;

    engine.stats.pbSize = sizeof( APIPB );
    engine.stats.payloadSize = sizeof( Payload );
    engine.stats.bufferSize = sizeof( APIBuffer );

    // 3. Thread the now-zeroed storage into the pools
    for ( uint32_t i = 0; i < APIPB_COUNT; i++ ) {
        _poolPush( (void**)&engine.pbPool, &engine.pbStorage[ i ], &engine.stats.pbFree );
    }

    for ( uint32_t i = 0; i < API_PAYLOAD_COUNT; i++ ) {
        _poolPush( (void**)&engine.payloadPool, &engine.payloadStorage[ i ], &engine.stats.payloadFree );
    }

    for ( uint32_t i = 0; i < API_BUFFER_COUNT; i++ ) {
        _poolPush( (void**)&engine.bufferPool, &engine.bufferStorage[ i ], &engine.stats.bufferFree );
    }
}

// Stats for debugging and monitoring
APICoreStatsRef APICoreGetStats( void ) {

    engine.stats.inputQueued = engine.inputQueue.count;
    engine.stats.outputQueued = engine.outputQueue.count;

    engine.stats.pbMemUsed = ( APIPB_COUNT - engine.stats.pbFree ) * sizeof( APIPB );
    engine.stats.payloadMemUsed = ( API_PAYLOAD_COUNT - engine.stats.payloadFree ) * sizeof( Payload );
    engine.stats.bufferMemUsed = ( API_BUFFER_COUNT - engine.stats.bufferFree ) * sizeof( APIBuffer );

    return (APICoreStatsRef)&engine.stats;
}

// Requests received from transport layer
APIPBQueueRef GetInputQueue( void ) {
    return (APIPBQueueRef)&engine.inputQueue;
}

// Completed responses for transport layer
APIBufferQueueRef GetOutputQueue( void ) {
    return (APIBufferQueueRef)&engine.outputQueue;
}

// Grab an APIPB from the pool
APIPBPtr AcquirePB( void ) {
    APIPBPtr pb = (APIPBPtr)_poolPop( (void**)&engine.pbPool, &engine.stats.pbFree, &engine.stats.pbPeak, APIPB_COUNT );
    if ( pb ) {
        memset( pb, 0, APIPB_SIZE );
        pb->origin = API_MODE_UNDETERMINED;
    }
    return pb;
}

// Recycle the tokens and payloads attached to a PB (stack or pool)
void ReleasePBMembers( APIPBPtr pb ) {

    if ( !pb ) {
        return;
    }

    // Recycle Payload chain
    while ( pb->payload ) {
        PayloadPtr next = pb->payload->next;
        ReleasePayload( pb->payload );
        pb->payload = next;
    }
    pb->payload = NULL;
}

// Return an APIPB to the pool
void ReleasePB( APIPBPtr pb ) {

    if ( !pb ) {
        return;
    }

    ReleasePBMembers( pb );
    _poolPush( (void**)&engine.pbPool, pb, &engine.stats.pbFree );
}

// Enqueue a completed APIPB to the specified queue
void EnqueuePB( APIPBQueueRef q, APIPBPtr pb ) {

    if ( !q || !pb ) {
        return;
    }

    pb->next = NULL;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if ( q->tail == NULL ) {
        q->head = q->tail = pb;
    } else {
        q->tail->next = pb;
        q->tail = pb;
    }

    q->count++;

    __set_PRIMASK( primask );
}

// Dequeue a completed APIPB from the specified queue, or NULL if empty
APIPBPtr DequeuePB( APIPBQueueRef q ) {
    if ( !q ) {
        return NULL;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    APIPBPtr pb = q->head;
    if ( pb ) {
        q->head = pb->next;
        if ( q->head == NULL ) {
            q->tail = NULL;
        }
        q->count--;
    }

    __set_PRIMASK( primask );
    return pb;
}

// Enqueue a completed APIBuffer to the specified queue
void EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr b ) {

    if ( !q || !b ) {
        return;
    }
    b->next = NULL;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if ( q->tail == NULL ) {
        q->head = q->tail = b;
    } else {
        q->tail->next = b;
        q->tail = b;
    }

    q->count++;

    __set_PRIMASK( primask );
}

// Dequeue a completed APIBuffer from the specified queue, or NULL if empty
APIBufferPtr DequeueBuffer( APIBufferQueueRef q ) {

    if ( !q ) {
        return NULL;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    APIBufferPtr b = q->head;

    if ( b ) {
        q->head = b->next;
        if ( q->head == NULL ) {
            q->tail = NULL;
        }
        q->count--;
    }

    __set_PRIMASK( primask );
    return b;
}

// Grab a payload from the pool
PayloadPtr AcquirePayload( void ) {
    return (PayloadPtr)_poolPop( (void**)&engine.payloadPool, &engine.stats.payloadFree, &engine.stats.payloadPeak,
                                 API_PAYLOAD_COUNT );
}

// Return a payload to the pool
void ReleasePayload( PayloadPtr p ) {
    if ( p ) {
        memset( p->data, 0, API_PAYLOAD_SIZE );
        _poolPush( (void**)&engine.payloadPool, p, &engine.stats.payloadFree );
    }
}

// Grab a buffer from the pool
APIBufferPtr AcquireBuffer( void ) {
    return (APIBufferPtr)_poolPop( (void**)&engine.bufferPool, &engine.stats.bufferFree, &engine.stats.bufferPeak,
                                   API_BUFFER_COUNT );
}

// Return a buffer to the pool
void ReleaseBuffer( APIBufferPtr b ) {
    if ( b ) {
        memset( b->data, 0, API_BUFFER_SIZE );
        b->length = 0;
        _poolPush( (void**)&engine.bufferPool, b, &engine.stats.bufferFree );
    }
}
