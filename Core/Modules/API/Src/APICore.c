/// @file APICore.c
///
/// @brief API engine — fixed-size memory pools and FIFO queues for APIPB and APIBuffer objects.
///
/// APICoreInit() zeroes all storage and threads every object onto its free-list pool.
/// AcquirePB / ReleasePB, AcquirePayload / ReleasePayload, and AcquireBuffer /
/// ReleaseBuffer are the only allocation entry points. EnqueuePB / DequeuePB and
/// EnqueueBuffer / DequeueBuffer manage the input (requests) and output (serialised
/// responses) FIFOs. All pool and queue operations are protected by FreeRTOS
/// critical sections so they are safe to call from any task or ISR context.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "APITask.h"
#include "APICore.h"
#include "APITypes.h"
#include "APICodec.h"
#include "APIRoutes.h"
#include "Platform.h"

/// @brief Singly-linked FIFO queue for APIPB packet buffers.
typedef struct APIPBQueue {
    APIPBPtr head;    ///< Oldest item (dequeue end).
    APIPBPtr tail;    ///< Newest item (enqueue end).
    uint32_t count;   ///< Number of items currently queued.
} APIPBQueue, *APIPBQueuePtr;

/// @brief Singly-linked FIFO queue for serialised output APIBuffer chains.
typedef struct APIBufferQueue {
    APIBufferPtr head;  ///< Oldest item (dequeue end).
    APIBufferPtr tail;  ///< Newest item (enqueue end).
    uint32_t     count; ///< Number of items currently queued.
} APIBufferQueue, *APIBufferQueuePtr;

#define APIPB_SIZE sizeof( APIPB )

/// @brief All API engine state — pools, queues, and diagnostics — in a single zero-able struct.
static struct {
    APIPB          pbStorage[ kApiPbCount ];
    Payload        payloadStorage[ kApiPayloadCount ];
    APIBuffer      bufferStorage[ kApiBufferCount ];

    APIPBPtr       pbPool;
    PayloadPtr     payloadPool;
    APIBufferPtr   bufferPool;

    APIPBQueue     inputQueue;
    APIBufferQueue outputQueue;

    APIBufferQueue extraQueues[ 4 ];
    int            extraQueueCount;

    APICoreStats   stats;
} engine;

/// @brief Push an item onto the head of an intrusive free-list pool.
///
/// The first word of @p item is overwritten with the current pool head pointer,
/// making the pool a singly-linked stack. Safe to call from any context.
///
/// @param[in,out] pool  Pointer to the pool head pointer.
/// @param[in]     item  Object to return; the caller must not use it after this call.
/// @param[in,out] count Optional free-count to increment; may be NULL.
static void _poolPush( void** pool, void* item, uint32_t* count ) {
    if ( !item ) return;

    taskENTER_CRITICAL();
    *(void**)item = *pool;
    *pool = item;
    if ( count ) ( *count )++;
    taskEXIT_CRITICAL();
}

/// @brief Pop an item from the head of an intrusive free-list pool.
///
/// Updates the optional free-count and peak-usage high-water mark.
/// Returns NULL if the pool is empty.
///
/// @param[in,out] pool   Pointer to the pool head pointer.
/// @param[in,out] count  Optional free-count to decrement; may be NULL.
/// @param[in,out] peak   Optional peak-in-use counter; may be NULL.
/// @param[in]     total  Total capacity of the pool (used to compute in-use count).
/// @return Pointer to the popped item, or NULL if the pool is empty.
static void* _poolPop( void** pool, uint32_t* count, uint32_t* peak, uint32_t total ) {
    taskENTER_CRITICAL();
    void* item = *pool;
    if ( item ) {
        *pool = *(void**)item;
        if ( count ) ( *count )--;
        if ( count && peak ) {
            uint32_t inUse = total - *count;
            if ( inUse > *peak ) *peak = inUse;
        }
    }
    taskEXIT_CRITICAL();
    return item;
}

/// @brief Initialise the API engine — zero all storage and populate the free-list pools.
///
/// Must be called once before any other APICore function. APITaskInit() calls this
/// after waiting for FlagSystemInitialised. All previously acquired objects are
/// invalidated; do not call after startup.
void APICoreInit( void ) {
    memset( &engine, 0, sizeof( engine ) );
    engine.stats.pbCount = kApiPbCount;
    engine.stats.payloadCount = kApiPayloadCount;
    engine.stats.bufferCount = kApiBufferCount;
    engine.stats.pbSize = sizeof( APIPB );
    engine.stats.payloadSize = sizeof( Payload );
    engine.stats.bufferSize = sizeof( APIBuffer );

    for ( uint32_t i = 0; i < kApiPbCount; i++ ) {
        _poolPush( (void**)&engine.pbPool, &engine.pbStorage[ i ], &engine.stats.pbFree );
    }
    for ( uint32_t i = 0; i < kApiPayloadCount; i++ ) {
        _poolPush( (void**)&engine.payloadPool, &engine.payloadStorage[ i ], &engine.stats.payloadFree );
    }
    for ( uint32_t i = 0; i < kApiBufferCount; i++ ) {
        _poolPush( (void**)&engine.bufferPool, &engine.bufferStorage[ i ], &engine.stats.bufferFree );
    }
}

/// @brief Snapshot current engine statistics into the APICoreStats struct and return a pointer.
///
/// The returned pointer is valid until the next APICoreInit() call. The live-count
/// fields (inputQueued, outputQueued, *MemUsed) are refreshed on each call; pool
/// free-counts and peak marks are maintained incrementally by the pool helpers.
///
/// @return Read-only pointer to the internal APICoreStats snapshot.
APICoreStatsRef APICoreGetStats( void ) {
    engine.stats.inputQueued = engine.inputQueue.count;
    engine.stats.outputQueued = engine.outputQueue.count;
    engine.stats.pbMemUsed = ( kApiPbCount - engine.stats.pbFree ) * sizeof( APIPB );
    engine.stats.payloadMemUsed = ( kApiPayloadCount - engine.stats.payloadFree ) * sizeof( Payload );
    engine.stats.bufferMemUsed = ( kApiBufferCount - engine.stats.bufferFree ) * sizeof( APIBuffer );
    return (APICoreStatsRef)&engine.stats;
}

/// @brief Return an opaque reference to the input (received-request) queue.
/// @return Queue reference for use with EnqueuePB() / DequeuePB().
APIPBQueueRef GetInputQueue( void ) { return (APIPBQueueRef)&engine.inputQueue; }

/// @brief Return an opaque reference to the output (serialised-response) queue.
/// @return Queue reference for use with EnqueueBuffer() / DequeueBuffer().
APIBufferQueueRef GetOutputQueue( void ) { return (APIBufferQueueRef)&engine.outputQueue; }

/// @brief Allocate an additional APIBufferQueue from the pre-allocated pool.
///
/// Only the existing enqueue/dequeue functions access internal fields via the opaque
/// pointer, so any APIBufferQueue allocated here is fully interoperable with them.
/// Returns NULL if the pool is exhausted.
///
/// @return Opaque queue reference, or NULL.
APIBufferQueueRef CreateBufferQueue( void ) {
    APIBufferQueueRef q = NULL;
    taskENTER_CRITICAL();
    if ( engine.extraQueueCount < 4 ) {
        q = (APIBufferQueueRef)&engine.extraQueues[ engine.extraQueueCount++ ];
        memset( q, 0, sizeof( APIBufferQueue ) );
    }
    taskEXIT_CRITICAL();
    return q;
}

/// @brief Acquire a zeroed APIPB from the pool.
/// @return Pointer to a clean APIPB, or NULL if the pool is exhausted.
APIPBPtr AcquirePB( void ) {
    APIPBPtr pb = (APIPBPtr)_poolPop( (void**)&engine.pbPool, &engine.stats.pbFree, &engine.stats.pbPeak, kApiPbCount );
    if ( pb ) {
        memset( pb, 0, APIPB_SIZE );
    }
    return pb;
}

/// @brief Release all Payload nodes attached to an APIPB without returning the PB itself.
///
/// Walks the pb->payload chain and calls ReleasePayload() on each node.
/// The PB remains valid and its payload pointer is set to NULL. Use this when
/// a handler has finished with the request payload but the response PB is still live.
void ReleasePBMembers( APIPBPtr pb ) {
    if ( !pb ) return;
    while ( pb->payload ) {
        PayloadPtr next = pb->payload->next;
        ReleasePayload( pb->payload );
        pb->payload = next;
    }
    pb->payload = NULL;
}

/// @brief Return an APIPB and all attached Payload nodes to their respective pools.
///
/// Calls ReleasePBMembers() first, then returns the PB. The caller must not
/// access @p pb after this call.
void ReleasePB( APIPBPtr pb ) {
    if ( !pb ) return;
    ReleasePBMembers( pb );
    _poolPush( (void**)&engine.pbPool, pb, &engine.stats.pbFree );
}

/// @brief Append an APIPB to the tail of a queue and wake the API task if it is the input queue.
///
/// Notifies the API task via vTaskNotifyGiveFromISR() or xTaskNotifyGive() depending
/// on whether the call originates from an ISR. This enqueue is the handoff from the
/// CDC receive path to the request-dispatch loop.
void EnqueuePB( APIPBQueueRef q, APIPBPtr pb ) {
    if ( !q || !pb ) return;
    pb->next = NULL;

    taskENTER_CRITICAL();
    if ( q->tail == NULL ) {
        q->head = q->tail = pb;
    } else {
        q->tail->next = pb;
        q->tail = pb;
    }
    q->count++;
    taskEXIT_CRITICAL();

    if ( q == GetInputQueue() ) {
        if ( __get_IPSR() != 0 ) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR( APITaskHandle, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        } else {
            xTaskNotifyGive( APITaskHandle );
        }
    }
}

/// @brief Remove and return the APIPB at the head of a queue, or NULL if empty.
///
/// @return Pointer to the dequeued APIPB, or NULL if the queue is empty.
APIPBPtr DequeuePB( APIPBQueueRef q ) {
    if ( !q ) return NULL;
    taskENTER_CRITICAL();
    APIPBPtr pb = q->head;
    if ( pb ) {
        q->head = pb->next;
        if ( q->head == NULL ) q->tail = NULL;
        q->count--;
    }
    taskEXIT_CRITICAL();
    return pb;
}

/// @brief Append a serialised APIBuffer chain to an output queue and wake the API task.
///
/// Notifies the API task so that USBCDCProcess() runs promptly. Works from task or
/// ISR context.
void EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr b ) {
    if ( !q || !b ) return;
    b->next = NULL;

    taskENTER_CRITICAL();
    if ( q->tail == NULL ) {
        q->head = q->tail = b;
    } else {
        q->tail->next = b;
        q->tail = b;
    }
    q->count++;
    taskEXIT_CRITICAL();

    if ( __get_IPSR() != 0 ) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTaskNotifyFromISR( APITaskHandle, 0x02, eSetBits, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    } else {
        xTaskNotify( APITaskHandle, 0x02, eSetBits );
    }
}

/// @brief Remove and return the APIBuffer at the head of a queue, or NULL if empty.
///
/// @return Pointer to the dequeued APIBuffer, or NULL if the queue is empty.
APIBufferPtr DequeueBuffer( APIBufferQueueRef q ) {
    if ( !q ) return NULL;
    taskENTER_CRITICAL();
    APIBufferPtr b = q->head;
    if ( b ) {
        q->head = b->next;
        if ( q->head == NULL ) q->tail = NULL;
        q->count--;
    }
    taskEXIT_CRITICAL();
    return b;
}

/// @brief Acquire a Payload node from the pool.
///
/// The data array is not zeroed; callers must initialise before use.
/// Returns NULL if the pool is exhausted.
///
/// @return Pointer to a Payload node, or NULL if none are available.
PayloadPtr AcquirePayload( void ) {
    return (PayloadPtr)_poolPop( (void**)&engine.payloadPool, &engine.stats.payloadFree, &engine.stats.payloadPeak, kApiPayloadCount );
}

/// @brief Zero a Payload node's data array and return it to the pool.
void ReleasePayload( PayloadPtr p ) {
    if ( p ) {
        memset( p->data, 0, kApiPayloadSize );
        _poolPush( (void**)&engine.payloadPool, p, &engine.stats.payloadFree );
    }
}

/// @brief Acquire a transmit APIBuffer from the pool.
///
/// Returns NULL if the pool is exhausted; the serialiser must abort and release
/// any already-acquired buffers in that case.
///
/// @return Pointer to a free APIBuffer, or NULL if none are available.
APIBufferPtr AcquireBuffer( void ) {
    return (APIBufferPtr)_poolPop( (void**)&engine.bufferPool, &engine.stats.bufferFree, &engine.stats.bufferPeak, kApiBufferCount );
}

/// @brief Zero a transmit APIBuffer and return it to the pool.
void ReleaseBuffer( APIBufferPtr b ) {
    if ( b ) {
        memset( b->data, 0, kApiBufferSize );
        b->length = 0;
        _poolPush( (void**)&engine.bufferPool, b, &engine.stats.bufferFree );
    }
}

/// @brief Dequeue and dispatch every pending request. Returns when the input queue is empty.
///
/// Calls the matched route handler for each request, releases the request PB,
/// and queues the response for transmission. Registered as TaskOwnerAPI in the
/// DriverRegistry and called from APITaskLoop.
void APICoreProcess( void ) {
    APIPBPtr req;
    while ( ( req = GetNextRequest() ) != NULL ) {
        if ( req->route && req->route->handler ) {
            APIPBPtr resp = req->route->handler( req );
            ReleasePB( req );
            if ( resp ) {
                APIQueueForSend( resp );
            }
        } else {
            ReleasePB( req );
        }
    }
}
