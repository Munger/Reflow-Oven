/// @file USBCDC.c
///
/// @brief USB CDC virtual COM port driver — InitModule, Open, GetStatus, and transmit pipeline.
///
/// USBCDCInitModule() creates per-instance event flag groups, initialises the API
/// core pools and stream parser, and signals FlagUSBCDCReady. USBCDCProcess() is
/// registered with TaskOwnerAPI in the DriverRegistry and called from APITaskLoop
/// after request dispatch. USBTxDoneHandler() is invoked directly from the USB HAL
/// ISR (usbd_cdc_if.c).
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "semphr.h"
#include "event_groups.h"

#include "APICore.h"
#include "APIRoutes.h"
#include "APICodec.h"
#include "USBCDC.h"
#include "usbd_cdc_if.h"

// ============================================================================
// Per-instance state
// ============================================================================

/// @brief Internal representation of a USB CDC instance.
typedef struct USBCDCInstance {
    USBCDCID         id;               ///< Instance identifier
    osEventFlagsId_t statusHandle;     ///< Per-instance event flag group
    StaticEventGroup_t statusBuffer;   ///< Storage backing statusHandle (no-heap allocation)
    SemaphoreHandle_t txDone;          ///< TX-completion binary semaphore (given by ISR)
    StaticSemaphore_t txDoneBuffer;    ///< Storage backing txDone (no-heap allocation)
    APIBufferQueueRef outputQueue;     ///< Per-instance serialised response queue
} USBCDCInstance, *USBCDCInstancePtr;

static USBCDCInstance instances[ USBCDCInstanceCount ];

// ============================================================================
// Module-level interface (called by ManagerTask via DriverRegistry)
// ============================================================================

/// @brief Initialise the CDC driver — zeros instances, creates status handles,
///        per-instance TX semaphores and output queues, initialises the API core
///        and stream parser, and signals global ready.
void USBCDCInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );

    APICoreInit();

    for ( int i = 0; i < USBCDCInstanceCount; i++ ) {
        instances[ i ].id           = (USBCDCID)i;
        instances[ i ].statusHandle = osEventFlagsNew( &(osEventFlagsAttr_t){ .cb_mem = &instances[ i ].statusBuffer, .cb_size = sizeof( StaticEventGroup_t ) } );
        instances[ i ].txDone       = xSemaphoreCreateBinaryStatic( &instances[ i ].txDoneBuffer );
        instances[ i ].outputQueue  = CreateBufferQueue();
    }

    APIStreamInit();

    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagUSBCDCReady ) );
}

USBCDCRef USBCDCOpen( USBCDCID id ) {
    if ( id >= USBCDCInstanceCount ) return NULL;

    USBCDCInstancePtr inst = &instances[ id ];
    osEventFlagsSet( inst->statusHandle, BIT( FlagUSBCDCStatusReady ) );
    return inst;
}

uint32_t USBCDCGetStatus( USBCDCRef cdc ) {
    if ( cdc == NULL ) return 0;
    return osEventFlagsGet( cdc->statusHandle );
}

// ============================================================================
// Transmit pipeline
// ============================================================================

/// @brief Dispatch pending requests and drain every instance's output queue.
///
/// Sets the serialiser's output queue context to each instance's queue so that
/// APIQueueForSend() routes responses to the correct port. After dispatching all
/// pending requests, drains each instance's output queue over CDC_Transmit_FS(),
/// blocking on the per-instance txDone semaphore between links. The ISR signals
/// completion by giving the instance's txDone semaphore.
void USBCDCProcess( void ) {
    for ( int i = 0; i < USBCDCInstanceCount; i++ ) {
        USBCDCInstancePtr inst = &instances[ i ];

        // Route serialised responses to this instance's output queue
        SetCurrentOutputQueue( inst->outputQueue );

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

        ResetCurrentOutputQueue();

        // Synchronous transmit — drain this instance's output queue.
        // xSemaphoreCreateBinary() gives initial count 0, so the first
        // xSemaphoreTake with 0 timeout returns immediately (pdFALSE).
        xSemaphoreTake( inst->txDone, 0 );

        APIBufferPtr buf;
        while ( ( buf = DequeueBuffer( inst->outputQueue ) ) != NULL ) {
            APIBufferPtr link = buf;
            while ( link ) {
                CDC_Transmit_FS( (uint8_t*) link->data, link->length );

                // Block until the ISR signals TX complete
                xSemaphoreTake( inst->txDone, portMAX_DELAY );

                APIBufferPtr next = link->next;
                ReleaseBuffer( link );
                link = next;
            }
        }
    }
}

/// @brief USB CDC TX-complete callback — unblocks USBCDCProcess via the instance 0 semaphore.
///
/// Gives the per-instance txDone semaphore so the blocked calling task can release
/// the completed buffer and submit the next link.
///
/// @warning Called from USB ISR context. Uses FromISR variants only.
void USBTxDoneHandler( void ) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR( instances[ USBCDC0 ].txDone, &xHigherPriorityTaskWoken );
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
