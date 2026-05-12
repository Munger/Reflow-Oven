#include <string.h>

#include "APITask.h"
#include "apicore.h"
#include "apitypes.h"
#include "stm32g0xx.h"

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

static struct {
    APIPB          pbStorage[ APIPB_COUNT ];
    Payload        payloadStorage[ API_PAYLOAD_COUNT ];
    APIBuffer      bufferStorage[ API_BUFFER_COUNT ];

    APIPBPtr       pbPool;
    PayloadPtr     payloadPool;
    APIBufferPtr   bufferPool;

    APIPBQueue     inputQueue;
    APIBufferQueue outputQueue;

    APICoreStats   stats;
} engine;

static void _poolPush( void** pool, void* item, uint32_t* count ) {
    if ( !item ) return;

    taskENTER_CRITICAL();
    *(void**)item = *pool;
    *pool = item;
    if ( count ) ( *count )++;
    taskEXIT_CRITICAL();
}

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

void APICoreInit( void ) {
    memset( &engine, 0, sizeof( engine ) );
    engine.stats.pbCount = APIPB_COUNT;
    engine.stats.payloadCount = API_PAYLOAD_COUNT;
    engine.stats.bufferCount = API_BUFFER_COUNT;
    engine.stats.pbSize = sizeof( APIPB );
    engine.stats.payloadSize = sizeof( Payload );
    engine.stats.bufferSize = sizeof( APIBuffer );

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

APICoreStatsRef APICoreGetStats( void ) {
    engine.stats.inputQueued = engine.inputQueue.count;
    engine.stats.outputQueued = engine.outputQueue.count;
    engine.stats.pbMemUsed = ( APIPB_COUNT - engine.stats.pbFree ) * sizeof( APIPB );
    engine.stats.payloadMemUsed = ( API_PAYLOAD_COUNT - engine.stats.payloadFree ) * sizeof( Payload );
    engine.stats.bufferMemUsed = ( API_BUFFER_COUNT - engine.stats.bufferFree ) * sizeof( APIBuffer );
    return (APICoreStatsRef)&engine.stats;
}

APIPBQueueRef GetInputQueue( void ) { return (APIPBQueueRef)&engine.inputQueue; }
APIBufferQueueRef GetOutputQueue( void ) { return (APIBufferQueueRef)&engine.outputQueue; }

APIPBPtr AcquirePB( void ) {
    APIPBPtr pb = (APIPBPtr)_poolPop( (void**)&engine.pbPool, &engine.stats.pbFree, &engine.stats.pbPeak, APIPB_COUNT );
    if ( pb ) {
        memset( pb, 0, APIPB_SIZE );
        pb->origin = API_MODE_UNDETERMINED;
    }
    return pb;
}

void ReleasePBMembers( APIPBPtr pb ) {
    if ( !pb ) return;
    while ( pb->payload ) {
        PayloadPtr next = pb->payload->next;
        ReleasePayload( pb->payload );
        pb->payload = next;
    }
    pb->payload = NULL;
}

void ReleasePB( APIPBPtr pb ) {
    if ( !pb ) return;
    ReleasePBMembers( pb );
    _poolPush( (void**)&engine.pbPool, pb, &engine.stats.pbFree );
}

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

    if ( q == GetOutputQueue() ) {
        if ( __get_IPSR() != 0 ) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xTaskNotifyFromISR( APITaskHandle, 0x02, eSetBits, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        } else {
            xTaskNotify( APITaskHandle, 0x02, eSetBits );
        }
    }
}

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

PayloadPtr AcquirePayload( void ) {
    return (PayloadPtr)_poolPop( (void**)&engine.payloadPool, &engine.stats.payloadFree, &engine.stats.payloadPeak, API_PAYLOAD_COUNT );
}

void ReleasePayload( PayloadPtr p ) {
    if ( p ) {
        memset( p->data, 0, API_PAYLOAD_SIZE );
        _poolPush( (void**)&engine.payloadPool, p, &engine.stats.payloadFree );
    }
}

APIBufferPtr AcquireBuffer( void ) {
    return (APIBufferPtr)_poolPop( (void**)&engine.bufferPool, &engine.stats.bufferFree, &engine.stats.bufferPeak, API_BUFFER_COUNT );
}

void ReleaseBuffer( APIBufferPtr b ) {
    if ( b ) {
        memset( b->data, 0, API_BUFFER_SIZE );
        b->length = 0;
        _poolPush( (void**)&engine.bufferPool, b, &engine.stats.bufferFree );
    }
}