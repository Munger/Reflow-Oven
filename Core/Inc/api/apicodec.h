#ifndef APISTREAM_H
#define APISTREAM_H

#include <stdint.h>

#include "apitypes.h"

void     APIStreamInit( void );

// Called directly by the USB ISR (CDC_Receive_FS) with whatever data arrived
void     ProcessStream( const uint8_t* data, uint32_t len );

// Fetch the next complete request, or NULL if none available.
// Caller must return the PB to the pool when done with ReleasePB()
APIPBPtr GetNextRequest( void );

void     APIQueueForSend( APIPBPtr pb );

#endif // APISTREAM_H