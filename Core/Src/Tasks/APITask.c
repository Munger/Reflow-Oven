#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "apicore.h"
#include "apiroutes.h"
#include "apihandlers.h"
#include "apicodec.h"
#include "usbd_cdc_if.h"
#include "APITask.h"

// Private state for the USB transmission machine
static APIBufferPtr currentUSBBuffer = NULL;

// Sends all pending buffers over USB. Called after processing a batch of requests or when a USB Tx completes.
// If the USB is busy, it sets a notification to try again when the current transmission finishes.
static void USBSendAll( void );

// Initialises the APITask and related modules. Called by app_freertos.c at startup.
void APITaskInit( void ) {
    APICoreInit();
    APIStreamInit();
}

// Main loop for the APITask. Waits for notifications of incoming requests or completed
// USB transmissions, processes requests, and sends responses.
// Called by app_freertos.c in the main task loop.
void APITaskLoop( void ) {
    uint32_t notification;
    
    // xTaskNotifyWait is a FreeRTOS function, portMAX_DELAY is a FreeRTOS constant
    xTaskNotifyWait( 0, 0xFFFFFFFF, &notification, portMAX_DELAY );

    APIPBPtr req;
    while ( ( req = GetNextRequest() ) != NULL ) {
        APIPB resp = { 0 }; 
        resp.origin = req->origin;
        resp.route  = req->route;

        if ( req->route && req->route->handler ) {
            // Handler owns req and must ReleasePB(req)
            resp.payload = req->route->handler( req );
        }

        if ( resp.payload ) {
            APIQueueForSend( &resp ); 
        }
    }
    USBSendAll();
}

// Sends all pending buffers over USB. Called after processing a batch of requests or when a USB Tx completes.
// If the USB is busy, it sets a notification to try again when the current transmission finishes.
static void USBSendAll( void ) {
    if ( currentUSBBuffer != NULL ) {
        if ( currentUSBBuffer->next != NULL ) {
            APIBufferPtr nextLink = currentUSBBuffer->next;

            if ( CDC_Transmit_FS( nextLink->data, nextLink->length ) == USBD_OK ) {
                APIBufferPtr finished = currentUSBBuffer;
                currentUSBBuffer = nextLink;
                ReleaseBuffer( finished );
            }
        }
        return;
    }

    currentUSBBuffer = DequeueBuffer( GetOutputQueue() );

    if ( currentUSBBuffer ) {
        if ( CDC_Transmit_FS( currentUSBBuffer->data, currentUSBBuffer->length ) != USBD_OK ) {
            xTaskNotify( APITaskHandle, 0x02, eSetBits );
        }
    }
}

void USBTxDoneHandler( void ) {
    APIBufferPtr finished = currentUSBBuffer;

    if ( !finished ) return;

    APIBufferPtr nextLink = finished->next;

    if ( nextLink ) {
        if ( CDC_Transmit_FS( nextLink->data, nextLink->length ) == USBD_OK ) {
            currentUSBBuffer = nextLink;
            ReleaseBuffer( finished );
        } else {
            xTaskNotify( APITaskHandle, 0x02, eSetBits );
        }
    } else {
        currentUSBBuffer = NULL;
        ReleaseBuffer( finished );

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR( APITaskHandle, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}