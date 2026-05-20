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

#endif // APICODEC_H
