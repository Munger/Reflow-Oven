/// @file APICodec.h
///
/// @brief USB stream parser and response serialiser — public interface.
///
/// ProcessStream() is called directly from the CDC_Receive_FS() USB ISR with each
/// raw receive buffer. APIQueueForSend() serialises a completed APIPB response and
/// enqueues the result for CDC transmission.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APICODEC_H
#define APICODEC_H

#include <stdint.h>

#include "APICore.h"
#include "APITypes.h"

/// @brief Initialise (or reset) the incremental stream parser. Call once at startup.
void     APIStreamInit( void );

/// @brief Consume a raw CDC receive buffer and advance the parser state machine.
///
/// May be called from ISR context (CDC_Receive_FS). Assembles complete APIPB
/// requests and enqueues them on the input queue, notifying the API task.
///
/// @param[in] data  Received bytes.
/// @param[in] len   Number of bytes in @p data.
void     ProcessStream( const uint8_t* data, uint32_t len );

/// @brief Dequeue the next complete parsed request, or NULL if none are available.
///
/// The caller takes ownership and must return the PB with ReleasePB() when done.
///
/// @return Pointer to the oldest queued APIPB, or NULL.
APIPBPtr GetNextRequest( void );

/// @brief Serialise a completed APIPB response and enqueue it for USB transmission.
///
/// Dispatches to SerialiseAPI() for API_MODE_API or SerialiseCLI() for all other
/// origins. Releases the PB and all attached payload back to their pools.
///
/// @param[in] pb  APIPB to serialise and release; may be NULL.
void     APIQueueForSend( APIPBPtr pb );

/// @brief Set the output queue that SerialiseAPI / SerialiseCLI will enqueue to.
///
/// Call before dispatching requests when responses must be routed to a specific
/// CDC instance's output queue rather than the default global one. Pass the queue
/// returned by CreateBufferQueue() or GetOutputQueue().
///
/// @param[in] q  Target output queue; NULL restores the default.
void                SetCurrentOutputQueue( APIBufferQueueRef q );

/// @brief Return the currently set output queue, or the default if none was set.
/// @return Active APIBufferQueueRef (never NULL — falls back to GetOutputQueue()).
APIBufferQueueRef   GetCurrentOutputQueue( void );

/// @brief Restore the default output queue (equivalent to SetCurrentOutputQueue(NULL)).
void                ResetCurrentOutputQueue( void );

#endif // APICODEC_H
