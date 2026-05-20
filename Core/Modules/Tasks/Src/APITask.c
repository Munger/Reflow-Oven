/// @file APITask.c
///
/// @brief USB REST API task — request dispatch and transmit pipeline implementation.
///
/// Waits for task notifications from the USB ISR (new data received or TX complete)
/// and the API core (request enqueued). Dispatches requests to registered route
/// handlers, serialises responses into APIBuffer chains, and drives the CDC
/// transmit pipeline. The USBSendAll() helper is called after each request batch
/// and on every TX-done notification to keep the pipeline drained.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "SystemStatusFlags.h"
#include "APICore.h"
#include "APIRoutes.h"
#include "APIHandlers.h"
#include "APICodec.h"
#include "usbd_cdc_if.h"
#include "APITask.h"

/// @brief Pointer to the APIBuffer currently being transmitted over USB.
///
/// Non-NULL while a CDC_Transmit_FS() call is in flight. The USB ISR clears
/// this via USBTxDoneHandler() once the hardware confirms completion.
static APIBufferPtr currentUSBBuffer = NULL;

static void USBSendAll( void );

/// @brief Initialise the API task — waits for system init then starts API subsystems.
///
/// Blocks on FlagSystemInitialised to ensure all drivers are ready before
/// accepting USB requests. Initialises the API core pool and stream parser.
/// Called once by app_freertos.c at startup.
void APITaskInit( void ) {
    osEventFlagsWait( SystemStatusFlagsHandle, BIT( FlagSystemInitialised ), osFlagsWaitAll | osFlagsNoClear, osWaitForever );
    APICoreInit();
    APIStreamInit();
}

/// @brief API task main loop body — processes requests and drives the transmit queue.
///
/// Blocks on xTaskNotifyWait() until woken by a USB receive, TX complete, or
/// APICore request-enqueue notification. Drains the input queue, dispatches each
/// request to its route handler, and calls USBSendAll() to flush any pending output.
///
/// Called repeatedly by app_freertos.c in the task's infinite loop.
void APITaskLoop( void ) {
    uint32_t notification;

    xTaskNotifyWait( 0, 0xFFFFFFFF, &notification, portMAX_DELAY );

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
    USBSendAll();
}

/// @brief Drain the output queue and push the next buffer to CDC_Transmit_FS().
///
/// If a transmission is already in progress, attempts to chain the next buffer
/// immediately rather than waiting for a TX-done notification. If the CDC layer
/// is busy, sets a notification so the task retries on the next wake.
///
/// @note Reentrant from task context only. Not safe to call from ISR.
static void USBSendAll( void ) {
    if ( currentUSBBuffer != NULL ) {
        if ( currentUSBBuffer->next != NULL ) {
            APIBufferPtr nextLink = currentUSBBuffer->next;

            if ( CDC_Transmit_FS( (uint8_t*) nextLink->data, nextLink->length ) == USBD_OK ) {
                APIBufferPtr finished = currentUSBBuffer;
                currentUSBBuffer = nextLink;
                ReleaseBuffer( finished );
            }
        }
        return;
    }

    currentUSBBuffer = DequeueBuffer( GetOutputQueue() );

    if ( currentUSBBuffer ) {
        if ( CDC_Transmit_FS( (uint8_t*) currentUSBBuffer->data, currentUSBBuffer->length ) != USBD_OK ) {
            xTaskNotify( APITaskHandle, 0x02, eSetBits );
        }
    }
}

/// @brief USB CDC TX-complete callback — advances or closes the transmit pipeline.
///
/// Releases the completed buffer. If a next link is available in the chain,
/// starts it immediately via CDC_Transmit_FS(). Otherwise resets currentUSBBuffer
/// to NULL and wakes the API task so it can dequeue the next buffer.
///
/// @warning Called from USB ISR context. Uses FromISR notification variants only.
void USBTxDoneHandler( void ) {
    APIBufferPtr finished = currentUSBBuffer;

    if ( !finished ) return;

    APIBufferPtr nextLink = finished->next;

    if ( nextLink ) {
        if ( CDC_Transmit_FS( (uint8_t*) nextLink->data, nextLink->length ) == USBD_OK ) {
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
