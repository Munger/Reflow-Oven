/// @file APICore.h
///
/// @brief API engine public interface — pool allocators, queue accessors, and diagnostics.
///
/// All allocation and queue operations are protected by FreeRTOS critical sections
/// and are safe to call from any task or ISR context. Pool storage is statically
/// allocated; there is no heap usage in the API subsystem.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APICORE_H
#define APICORE_H

#include <stdint.h>

#include "APITypes.h"

/// @brief Opaque handle to an APIPBQueue — prevents direct struct access outside APICore.c.
typedef struct APIPBQueue*     APIPBQueueRef;

/// @brief Opaque handle to an APIBufferQueue — prevents direct struct access outside APICore.c.
typedef struct APIBufferQueue* APIBufferQueueRef;

/// @brief Initialise pools and queues. Call once at startup before any other APICore function.
void                           APICoreInit( void );

/// @brief Return the input queue — received requests awaiting dispatch.
APIPBQueueRef                  GetInputQueue( void );

/// @brief Return the output queue — serialised responses awaiting transmission.
APIBufferQueueRef              GetOutputQueue( void );

/// @brief Acquire a zeroed APIPB from the pool.
/// @return Pointer to a clean APIPB, or NULL if the pool is exhausted.
APIPBPtr                       AcquirePB( void );

/// @brief Release an APIPB and all attached Payload nodes back to their pools.
/// @param[in] pb  APIPB to release; ignores NULL.
void                           ReleasePB( APIPBPtr pb );

/// @brief Append an APIPB to the tail of @p q; notifies the API task if q is the input queue.
/// @param[in] q   Target queue.
/// @param[in] pb  APIPB to enqueue.
void                           EnqueuePB( APIPBQueueRef q, APIPBPtr pb );

/// @brief Remove and return the APIPB at the head of @p q, or NULL if empty.
/// @param[in] q  Source queue.
/// @return Oldest queued APIPB, or NULL.
APIPBPtr                       DequeuePB( APIPBQueueRef q );

/// @brief Append an APIBuffer chain to the tail of @p q; notifies the API task if q is the output queue.
/// @param[in] q  Target queue.
/// @param[in] b  Head of the APIBuffer chain to enqueue.
void                           EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr b );

/// @brief Remove and return the APIBuffer at the head of @p q, or NULL if empty.
/// @param[in] q  Source queue.
/// @return Oldest queued APIBuffer, or NULL.
APIBufferPtr                   DequeueBuffer( APIBufferQueueRef q );

/// @brief Acquire a Payload node from the pool.
/// @return Pointer to a Payload, or NULL if the pool is exhausted.
PayloadPtr                     AcquirePayload( void );

/// @brief Return a Payload node to the pool after zeroing its data array.
/// @param[in] p  Payload to release; ignores NULL.
void                           ReleasePayload( PayloadPtr p );

/// @brief Release all Payload nodes attached to @p pb without returning the PB itself.
/// @param[in] pb  APIPB whose payload chain should be freed; ignores NULL.
void                           ReleasePBMembers( APIPBPtr pb );

/// @brief Acquire a transmit APIBuffer from the pool.
/// @return Pointer to a free APIBuffer, or NULL if the pool is exhausted.
APIBufferPtr                   AcquireBuffer( void );

/// @brief Return a transmit APIBuffer to the pool after zeroing its data and resetting length.
/// @param[in] b  Buffer to release; ignores NULL.
void                           ReleaseBuffer( APIBufferPtr b );

/// @brief Live diagnostics snapshot for the API memory engine.
typedef struct APICoreStats {
    uint32_t pbFree;         ///< APIPB nodes currently in the free pool.
    uint32_t payloadFree;    ///< Payload nodes currently in the free pool.
    uint32_t bufferFree;     ///< APIBuffer nodes currently in the free pool.
    uint32_t inputQueued;    ///< Requests currently waiting in the input queue.
    uint32_t outputQueued;   ///< Response buffers currently waiting in the output queue.

    uint32_t pbPeak;         ///< Peak simultaneous APIPB nodes in use (high-water mark).
    uint32_t payloadPeak;    ///< Peak simultaneous Payload nodes in use (high-water mark).
    uint32_t bufferPeak;     ///< Peak simultaneous APIBuffer nodes in use (high-water mark).

    uint32_t pbCount;        ///< Total APIPB pool capacity.
    uint32_t payloadCount;   ///< Total Payload pool capacity.
    uint32_t bufferCount;    ///< Total APIBuffer pool capacity.

    size_t   pbSize;         ///< Size in bytes of one APIPB.
    size_t   payloadSize;    ///< Size in bytes of one Payload.
    size_t   bufferSize;     ///< Size in bytes of one APIBuffer.

    size_t   pbMemUsed;      ///< Total bytes currently in use for APIPBs.
    size_t   payloadMemUsed; ///< Total bytes currently in use for Payloads.
    size_t   bufferMemUsed;  ///< Total bytes currently in use for APIBuffers.
} APICoreStats, *APICoreStatsPtr;

/// @brief Read-only pointer to the internal APICoreStats snapshot.
typedef const APICoreStats* APICoreStatsRef;

/// @brief Refresh and return a snapshot of the current API engine statistics.
/// @return Read-only pointer to the internal APICoreStats; valid until next APICoreInit().
APICoreStatsRef             APICoreGetStats( void );

#endif // APICORE_H
